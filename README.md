# kvm-hypervisor

A minimal type-2 hypervisor built directly on the Linux **KVM API**. It boots
one or more 64-bit guests in parallel — one POSIX thread and one vCPU per guest
— and provides them with serial I/O, a sandboxed file API, and an
interrupt-driven shared buffer for inter-VM communication.

## What it does

Guests are freestanding programs: no operating system, no libc, no runtime.
They talk to the hypervisor exclusively through port-mapped I/O, and the
hypervisor performs the privileged work on their behalf.

**Core hypervisor.** Creates a VM through `/dev/kvm`, maps guest memory, builds
identity-mapped page tables (4KB or 2MB pages), switches the vCPU into long
mode, loads the guest image, and drives the `KVM_RUN` loop. Serial output and
input go through port `0xE9`. A guest that faults is torn down on its own —
the remaining VMs keep running, unaffected.

**File API.** A guest-facing `open`/`close`/`read`/`write`/`lseek` implemented
as a byte-level protocol over port `0x0278`, with a per-VM state machine on the
hypervisor side. Every VM is confined to its own directory, so it can only ever
reach its own files. Files declared shared on the command line are readable by
everyone and copy-on-write on first write, so the original is never modified.

**Inter-VM communication.** A shared byte buffer coordinated by the hypervisor.
Roles (writer / reader) are assigned at runtime and delivered to guests by
injecting interrupt vector 32. A transfer proceeds in rounds: the writer fills
the buffer, every live reader is woken and must acknowledge, and only then does
the writer get its next round. If any VM dies mid-transfer — writer or reader —
the remaining ones still terminate cleanly instead of hanging.

## Design notes

A few decisions worth calling out, since they're what the implementation is
actually about:

- **One general-purpose guest image.** `guest.img` learns whether it is the
  writer or a reader by asking the hypervisor on its first interrupt, so the
  same binary serves either role. Running three VMs instead of two is a change
  to the command line, not to the code.
- **No condition variables.** The whole buffer protocol runs under a single
  mutex, and "waiting" is a per-VM pending-interrupt counter plus a poll loop in
  the guest. A guest with nothing to do reads port `0x511`; each of those VM
  exits is the hypervisor's opportunity to inject a pending interrupt.
- **Isolation by construction.** Per-VM confinement isn't a permission check —
  every path a guest opens is built under `local_vm<id>/`. Filename validation
  then rejects anything that could escape it (`..`, absolute paths), since
  each path component must begin with a letter.
- **Graceful VM death.** `buffer_vm_died` reconciles the round bookkeeping when
  a VM disappears: a dead reader is dropped from the pending count, and a dead
  writer causes a zero-length final round so readers shut down instead of
  waiting forever.

## Repository layout

```
.
├── host/                  the hypervisor (an ordinary Linux program)
│   ├── inc/               vm.h, args.h, files.h, buffer.h
│   ├── src/
│   │   ├── main.c         entry point, per-VM thread, KVM_RUN loop, port dispatch
│   │   ├── args.c         command-line parsing
│   │   ├── vm.c           KVM setup, paging, long mode, image loading, IRQ injection
│   │   ├── files.c        file protocol, per-VM sandboxing, copy-on-write
│   │   └── buffer.c       shared buffer, transfer rounds, interrupt bookkeeping
│   └── Makefile
└── guest/                 guest code (freestanding, no libc)
    ├── inc/               io.h, descriptors.h, lib.h, file.h, interrupts.h
    ├── src/
    │   ├── main.c         general-purpose guest (serial + files + shared buffer)
    │   ├── main_a_*.c     demo guests exercising the core hypervisor
    │   ├── main_b_*.c     demo guests exercising the file API
    │   ├── main_v_demo.c  transfer demo printing per-round statistics
    │   ├── lib.c          GDT/IDT setup, serial print helpers
    │   ├── file.c         guest side of the file protocol
    │   └── interrupts.c   IDT construction and interrupt entry points
    ├── guest.ld           linker script (entry point and load address)
    └── Makefile
```

Every `guest/src/main*.c` builds into its own image: `main.c` becomes
`guest.img`, `main_a_serial.c` becomes `guest_a_serial.img`, and so on.
`file.c`, `lib.c`, and `interrupts.c` are shared modules linked into each.
Dropping a new `main_<name>.c` into `guest/src/` is all it takes to add a
guest — the Makefile picks it up automatically.

| Image | Demonstrates |
|-------|--------------|
| `guest.img` | general-purpose guest; takes the writer or reader role assigned at runtime |
| `guest_a_pozdrav.img` | minimal guest — writes a greeting to the serial port |
| `guest_a_serial.img` | reads from the serial port and echoes the input back |
| `guest_a_crash.img` | faults deliberately (`ud2`) to show isolated VM failure |
| `guest_b_local.img` | reads a shared file, writes and reads a private one, seeks |
| `guest_b_shared.img` | writes to a shared file, triggering copy-on-write |
| `guest_b_demo.img` | full file-API walkthrough: rejected names, private file, shared file + COW |
| `guest_v_demo.img` | buffer transfer with per-round send/receive statistics |

## Building

Requires Linux with hardware virtualization available and access to
`/dev/kvm`.

```sh
make -C host
make -C guest
```

If `/dev/kvm` isn't accessible:

```sh
sudo modprobe kvm_intel     # kvm_amd on AMD CPUs
sudo chmod 666 /dev/kvm
```

Under WSL2, if the device doesn't exist at all, enable nested virtualization in
`.wslconfig` (`[wsl2]` / `nestedVirtualization=true`), then `wsl --shutdown` and
reopen.

## Usage

```sh
./host/build/hypervisor -m <2|4|8> -p <4|2> -g <image> [image ...] [-f <file> ...]
```

| Option | Meaning |
|--------|---------|
| `-m`, `--memory` | guest memory in MB — `2`, `4`, or `8` |
| `-p`, `--page` | page size — `4` for 4KB, `2` for 2MB |
| `-g`, `--guest` | one or more guest images; one VM per image |
| `-f`, `--file` | files shared between VMs |

Invalid option values are rejected with a message and a non-zero exit status.

### Examples

```sh
# Two VMs, printing and halting
./host/build/hypervisor -m 4 -p 2 -g guest/build/guest_a_pozdrav.img guest/build/guest_a_pozdrav.img

# A crashing VM doesn't affect the other
./host/build/hypervisor -m 2 -p 4 -g guest/build/guest_a_crash.img guest/build/guest_a_pozdrav.img

# File API, including copy-on-write over a shared file
echo "shared file contents" > deljeni.txt
./host/build/hypervisor -m 4 -p 2 -g guest/build/guest_b_demo.img -f deljeni.txt
cat deljeni.txt              # untouched
cat local_vm0/deljeni.txt    # the VM's private copy

# Inter-VM transfer: VM 0 writes, the rest read
seq 1 20 > ulaz.txt
./host/build/hypervisor -m 4 -p 2 -g guest/build/guest.img guest/build/guest.img guest/build/guest.img -f ulaz.txt
diff ulaz.txt local_vm1/izlaz.txt    # empty — byte-for-byte identical
```

Each VM creates a `local_vm<id>/` directory in the working directory for its
private files. Clear them between runs with `rm -rf local_vm*`.

> When several VMs write to the serial port at once, their output interleaves
> byte by byte — an expected consequence of running in parallel threads. File
> contents are always consistent, so `cat` and `diff` are the reliable check.

## Implementation reference

| Port | Purpose |
|------|---------|
| `0xE9` | serial output and input, one byte at a time |
| `0x0278` | file protocol (operation code, arguments, then result) |
| `0x510` | role assignment and buffer data |
| `0x511` | poll port — how a waiting guest yields to the hypervisor |
| `0x520` | acknowledgements (bytes written / bytes read) |

Guest memory is identity-mapped. Page tables live at the bottom of guest
memory (PML4 at `0x1000`, PDPT at `0x2000`, PD at `0x3000`, page tables from
`0x4000`), the guest image is loaded at `0x8000`, and the stack starts at the
top of memory. With 2MB pages the PD entries carry the `PS` bit directly and
there is no PT level; with 4KB pages one page table covers each 2MB of memory.

> Note: source comments, console messages, and the hardcoded filenames used by
> the demo guests (`ulaz.txt`, `izlaz.txt`, `deljeni.txt`, `privatni.txt`) are
> in Serbian, matching the coursework this was written for. Only this README,
> the license, and `.gitignore` are in English.

## License

MIT — see [LICENSE](LICENSE).
