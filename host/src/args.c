#include "args.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>  // za parsiranje argumenata komandne linije

void print_usage(const char *prog)
{
	fprintf(stderr,
		"Koriscenje: %s -m <2|4|8> -p <4|2> -g <img> [img ...] [-f <fajl> [fajl ...]]\n"
		"  -m, --memory  velicina memorije gosta u MB (2, 4 ili 8)\n"
		"  -p, --page    velicina stranice (4 -> 4KB, 2 -> 2MB)\n"
		"  -g, --guest   jedan ili vise fajlova sa izvrsnim kodom gosta\n"
		"  -f, --file    dijeljeni fajlovi izmedju virtuelnih masina\n",
		prog);
}

static int gather_args(int argc, char *argv[], char *first, char **out, int max)
{
	int n = 0;
	out[n++] = first;

	while (optind < argc && argv[optind][0] != '-' && n < max)
		out[n++] = argv[optind++];
	return n;
}

int parse_args(int argc, char *argv[], struct args *cfg)
{
	static struct option long_opts[] = {
		{ "memory", required_argument, 0, 'm' },
		{ "page",   required_argument, 0, 'p' },
		{ "guest",  required_argument, 0, 'g' },
		{ "file",   required_argument, 0, 'f' },
		{ 0, 0, 0, 0 }
	};
	int opt, mem = 0, page = 0;
	char *end;

	memset(cfg, 0, sizeof(*cfg));

	while ((opt = getopt_long(argc, argv, "m:p:g:f:", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'm':
			mem = (int)strtol(optarg, &end, 10);
			if (*end != '\0' || (mem != 2 && mem != 4 && mem != 8)) {
				fprintf(stderr, "Greska: nevalidna vrijednost za --memory: %s (dozvoljeno: 2, 4 ili 8)\n", optarg);
				return -1;
			}
			break;
		case 'p':
			page = (int)strtol(optarg, &end, 10);
			if (*end != '\0' || (page != 4 && page != 2)) {
				fprintf(stderr, "Greska: nevalidna vrijednost za --page: %s (dozvoljeno: 4 za 4KB ili 2 za 2MB)\n", optarg);
				return -1;
			}
			break;
		case 'g':
			cfg->guest_count = gather_args(argc, argv, optarg, cfg->guest_images, MAX_VMS);
			break;
		case 'f':
			cfg->shared_file_count = gather_args(argc, argv, optarg, cfg->shared_files, MAX_VMS);
			break;
		default:
			return -1;
		}
	}

	if (mem == 0 || page == 0 || cfg->guest_count == 0) {
		fprintf(stderr, "Greska: opcije --memory, --page i --guest su obavezne\n");
		return -1;
	}

	cfg->mem_size = (size_t)mem * 1024u * 1024u;
	cfg->page_size = (page == 2) ? PAGE_SIZE_2MB : PAGE_SIZE_4KB;

	for (int i = 0; i < cfg->shared_file_count; i++) {
		if (access(cfg->shared_files[i], R_OK) != 0) {
			fprintf(stderr, "Greska: dijeljeni fajl '%s' ne postoji ili nije citljiv\n", cfg->shared_files[i]);
			return -1;
		}
	}

	return 0;
}
