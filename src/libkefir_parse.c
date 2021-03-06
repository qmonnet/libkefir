// SPDX-License-Identifier: BSD-2-Clause
/* Copyright (c) 2019 Netronome Systems, Inc. */

#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "libkefir_error.h"
#include "libkefir_parse.h"

DEFINE_ERR_FUNCTIONS("parser")

int parse_check_and_store_uint(unsigned int val, void *output, uint32_t nb_bits,
			       bool is_net_byte_order)
{
	unsigned int tmp = is_net_byte_order ? ntohl(val) : val;

	if (tmp > (unsigned long)(2 << (nb_bits - 1)) - 1) {
		err_fail("value %u is too big (expected lower than %u)", tmp,
			 2 << (nb_bits - 1));
		return -1;
	}

	if (nb_bits <= 8)
		*(uint8_t *)output = val;
	else if (nb_bits <= 16)
		*(uint16_t *)output = is_net_byte_order ? val : htons(val);
	else
		*(uint32_t *)output = is_net_byte_order ? val : htonl(val);
	return 0;
}

int parse_uint(const char *input, void *output, uint32_t nb_bits)
{
	unsigned int res;
	char *endptr;

	res = strtoul(input, &endptr, 0);
	if (*endptr != '\0') {
		err_fail("could not parse %s as int", input);
		return -1;
	}

	return parse_check_and_store_uint(res, output, nb_bits, false);
}

static void bitmask_from_int(uint8_t mask, uint8_t *bitmask, size_t size)
{
	size_t i;

	for (i = 0; i < size && mask > 0; i++, mask -= 8)
		bitmask[i] = mask > 8 ? 0xff : 0xff << (8 - mask);
}

int parse_uint_slash_mask(const char *input, void *output, uint32_t nb_bits,
			  uint8_t *mask)
{
	char *endptr, *slash;
	unsigned int res;

	slash = strchr(input, '/');
	if (slash)
		if (parse_uint(slash + 1, mask, nb_bits))
			return -1;

	res = strtoul(input, &endptr, 10);
	if (*endptr != '\0' && endptr != slash) {
		err_fail("could not parse %s as int", input);
		return -1;
	}

	return parse_check_and_store_uint(res, output, nb_bits, false);
}

int parse_eth_addr(const char *input, struct ether_addr *output)
{
	struct ether_addr *addr;

	addr = ether_aton(input);

	if (!addr) {
		err_fail("could not parse ether address %s", input);
		return -1;
	}

	memcpy(output, addr, sizeof(struct ether_addr));

	/* "addr" statically allocated in ether_aton(), do not free it */

	return 0;
}

int parse_eth_addr_slash_mask(const char *input, struct ether_addr *output,
			      uint8_t *mask)
{
	char *slash;

	slash = strchr(input, '/');
	if (slash) {
		struct ether_addr *mask_bits;

		mask_bits = ether_aton(slash + 1);

		if (mask_bits) {
			/* Mask expressed in the shape "/ff:ff:ff:00:00:00" */
			memcpy(mask, mask_bits, sizeof(struct ether_addr));
		} else {
			unsigned int mask_int;
			char *endptr;

			/* Mask may be an integer, as in "/24" */
			mask_int = strtoul(slash + 1, &endptr, 10);
			if (*endptr != '\0' || mask_int > 48) {
				err_fail("could not parse %s as mask",
					 slash + 1);
				return -1;
			}
			bitmask_from_int(mask_int, mask, 6);
		}
	}

	return parse_eth_addr(input, output);
}

static int parse_ip_addr(int af, const char *input, uint32_t *output)
{
	if (inet_pton(af, input, output) != 1) {
		err_fail("could not parse IP address %s", input);
		return -1;
	}

	return 0;
}

int parse_ipv4_addr(const char *input, uint32_t *output)
{
	return parse_ip_addr(AF_INET, input, output);
}

int parse_ipv6_addr(const char *input, uint32_t *output)
{
	return parse_ip_addr(AF_INET6, input, output);
}

static int
parse_slash_prefix_mask(const char *input, uint8_t *mask, uint8_t max_val)
{
	unsigned int mask_int;
	char *endptr;

	mask_int = strtoul(input, &endptr, 0);
	if (*endptr != '\0' || mask_int > max_val) {
		err_fail("could not parse %s as int mask (prefix length)",
			 input);
		return -1;
	}
	bitmask_from_int(mask_int, mask, max_val / 8);

	return 0;
}

static int
parse_ip_addr_slash_mask(int af, const char *input, uint32_t *output,
			 uint8_t *mask)
{
	char *slash, *input_cpy = (char *)input;
	int res;

	slash = strchr(input, '/');
	if (slash) {
		if (parse_slash_prefix_mask(slash + 1, mask,
					    af == AF_INET ? 32 : 128))
			return -1;

		input_cpy = strndup(input, slash - input);
	}

	res = parse_ip_addr(af, input_cpy, output);

	if (slash)
		free(input_cpy);

	return res;
}

int parse_ipv4_addr_slash_mask(const char *input, uint32_t *output,
			       uint8_t *mask)
{
	return parse_ip_addr_slash_mask(AF_INET, input, output, mask);
}

int parse_ipv6_addr_slash_mask(const char *input, uint32_t *output,
			       uint8_t *mask)
{
	return parse_ip_addr_slash_mask(AF_INET6, input, output, mask);
}
