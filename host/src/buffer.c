#include "buffer.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>

enum vstate {
	VS_MODE,  // ceka prvu obradu prekida
	VS_IDLE,
	VS_WDATA, // pisac: prima se niz bajtova tekuce runde
	VS_WACK,  // pisac: ceka da procita potvrdu sa 0x520
	VS_RDATA, // citalac: salju mu se bajtovi
	VS_RACK, // citalac: ceka da posalje potvrdu na 0x520
	VS_DONE
};

struct vm_state {
	int alive;
	int role;  // 1 - pisac, 0 - citalac
	int pending;  // koliko prekida ceka da se ubaci u trenutnu vm
	enum vstate state;
	int needs_round;
	uint32_t read_pos;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static struct vm_state vms[MAX_VMS];
static int n_vms;

static uint8_t buffer[BUFFER_SIZE];
static uint32_t buf_len;
static int round_active;
static int readers_left;
static int final_round;
static int transfer_done;
static int writer_dead;

static uint32_t write_count;
static uint32_t write_received;

void buffer_init(int n)
{
	n_vms = n;
	memset(vms, 0, sizeof(vms));
	memset(buffer, 0, sizeof(buffer));
	buf_len = 0;
	round_active = 0;
	readers_left = 0;
	final_round = 0;
	transfer_done = 0;
	writer_dead = 0;
	write_count = 0;
	write_received = 0;

	for (int i = 0; i < n; i++) {
		vms[i].alive = 1;
		vms[i].role = (i == 0) ? 1 : 0;
		vms[i].pending = 1;  // svakoj vm je prvi prekid dodjela uloge
		vms[i].state = VS_MODE;
	}
}

static void round_finished_locked(void);

static void commit_round_locked(uint32_t stored, int is_final)
{
	buf_len = stored;
	final_round = is_final;
	round_active = 1;
	readers_left = 0;

	for (int i = 0; i < n_vms; i++) {
		if (i == 0 || !vms[i].alive || vms[i].state == VS_DONE)
			continue;
		vms[i].needs_round = 1;
		vms[i].pending++;
		readers_left++;
	}

	if (readers_left == 0)
		round_finished_locked();
}

static void emit_final_locked(void)
{
	if (transfer_done || round_active)
		return;
	write_count = 0;
	commit_round_locked(0, 1);
}

static void round_finished_locked(void)
{
	round_active = 0;

	if (final_round) {
		transfer_done = 1;
		return;
	}

	if (!writer_dead && vms[0].alive)
		vms[0].pending++;
	else
		emit_final_locked();
}

static uint32_t get_u32(void *data) { uint32_t v; memcpy(&v, data, 4); return v; }

int buffer_io(int vm_id, int is_in, int size, uint16_t port, void *data)
{
	struct vm_state *v = &vms[vm_id];
	int ret = 0;

	pthread_mutex_lock(&lock);

	if (port == BUFFER_PORT_DATA && is_in) {
		switch (v->state) {
		case VS_MODE:
			if (size != 4) { ret = -1; break; }
			memcpy(data, &v->role, 4);
			v->state = VS_IDLE;
			if (v->role == 1)
				v->pending++;
			break;
		case VS_IDLE:
			if (v->role != 0 || !v->needs_round || size != 4) { ret = -1; break; }
			memcpy(data, &buf_len, 4);
			v->needs_round = 0;
			if (buf_len == 0)
				v->state = VS_RACK;
			else {
				v->state = VS_RDATA;
				v->read_pos = 0;
			}
			break;
		case VS_RDATA:
			if (size != 1) { ret = -1; break; }
			*(uint8_t *)data = buffer[v->read_pos++];
			if (v->read_pos == buf_len)
				v->state = VS_RACK;
			break;
		default:
			ret = -1;
			break;
		}
	} else if (port == BUFFER_PORT_DATA && !is_in) {
		switch (v->state) {
		case VS_IDLE:
			if (v->role != 1 || round_active || size != 4) { ret = -1; break; }
			write_count = get_u32(data);
			write_received = 0;
			if (write_count == 0) {
				commit_round_locked(0, 1);
				v->state = VS_WACK;
			} else {
				v->state = VS_WDATA;
			}
			break;
		case VS_WDATA:
			if (size != 1) { ret = -1; break; }
			if (write_received < BUFFER_SIZE)
				buffer[write_received] = *(uint8_t *)data;
			write_received++;
			if (write_received == write_count) {
				uint32_t stored = (write_count < BUFFER_SIZE) ? write_count : BUFFER_SIZE;
				commit_round_locked(stored, 0);
				v->state = VS_WACK;
			}
			break;
		default:
			ret = -1;
			break;
		}
	} else if (port == BUFFER_PORT_ACK && is_in) {
		uint32_t stored = (write_count < BUFFER_SIZE) ? write_count : BUFFER_SIZE;

		if (v->state != VS_WACK || size != 4) {
			ret = -1;
		} else {
			memcpy(data, &stored, 4);
			v->state = (write_count == 0) ? VS_DONE : VS_IDLE;
		}
	} else if (port == BUFFER_PORT_ACK && !is_in) {
		uint32_t n;

		if (v->state != VS_RACK || size != 4) {
			ret = -1;
		} else {
			n = get_u32(data);
			if (n != buf_len) {
				printf("[VM %d] Citalac je prijavio %u procitanih bajtova umjesto %u - zaustavlja se VM\n",
				       vm_id, n, buf_len);
				ret = -1;
			} else {
				v->state = final_round ? VS_DONE : VS_IDLE;
				readers_left--;
				if (readers_left == 0)
					round_finished_locked();
			}
		}
	} else {
		ret = -1;
	}

	pthread_mutex_unlock(&lock);
	return ret;
}

int buffer_irq_pending(int vm_id)
{
	int r;

	pthread_mutex_lock(&lock);
	r = vms[vm_id].pending > 0;
	pthread_mutex_unlock(&lock);
	return r;
}

int buffer_irq_take(int vm_id)
{
	int r;

	pthread_mutex_lock(&lock);
	r = vms[vm_id].pending > 0;
	if (r)
		vms[vm_id].pending--;
	pthread_mutex_unlock(&lock);
	return r;
}

void buffer_vm_died(int vm_id)
{
	struct vm_state *v = &vms[vm_id];

	pthread_mutex_lock(&lock);

	if (!v->alive) {
		pthread_mutex_unlock(&lock);
		return;
	}

	v->alive = 0;
	v->pending = 0;

	if (v->role == 0) {
		if (round_active &&
		    (v->needs_round || v->state == VS_RDATA || v->state == VS_RACK)) {
			v->needs_round = 0;
			readers_left--;
			if (readers_left == 0)
				round_finished_locked();
		}
	} else {
		if (!transfer_done) {
			writer_dead = 1;
			if (!round_active)
				emit_final_locked();
		}
	}

	pthread_mutex_unlock(&lock);
}
