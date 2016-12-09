#include <stdlib.h>
#include <string.h>
#include <sregex.h>

#include "text-regex.h"
#include "text-motions.h"

struct Regex {
	sre_pool_t *pool;     /* pool for parsing and compiling */
	sre_pool_t *mpool;    /* pool for matching */
	sre_regex_t *regex;
	sre_program_t *prog;
	sre_uint_t ncaptures;
	sre_int_t* captures;
};

static sre_uint_t regex_capture_size(Regex *r) {
	return 2 * sizeof(sre_int_t) * (r->ncaptures + 1);
}

static int translate_cflags(int cflags) {
	return (cflags & REG_ICASE) ? SRE_REGEX_CASELESS : 0;
}

static sre_vm_pike_ctx_t *make_context(Regex *r) {
	return sre_vm_pike_create_ctx(r->mpool, r->prog, r->captures, regex_capture_size(r));
}

static void destroy_context(sre_vm_pike_ctx_t *ctx, Regex *r) {
	sre_reset_pool(r->mpool);
}

Regex *text_regex_new(void) {
	return calloc(1, sizeof(Regex));
}

int text_regex_compile(Regex *r, const char *string, int cflags) {
	sre_int_t err_offset;
	r->pool = sre_create_pool(4096);

	if (!r->pool)
		return 1;
	r->regex = sre_regex_parse(r->pool, (sre_char *) string, &r->ncaptures,
	                           translate_cflags(cflags), &err_offset);
	if (!r->regex)
		goto err;
	r->prog = sre_regex_compile(r->pool, r->regex);
	if (!r->prog)
		goto err;
	r->captures = malloc(regex_capture_size(r));
	if (!r->captures)
		goto err;
	r->mpool = sre_create_pool(4096);
	if (!r->mpool)
		goto err;
	return 0;
err:
	sre_destroy_pool(r->pool);
	return 1;
}

void text_regex_free(Regex *r) {
	if (!r)
		return;
	if (r->pool)
		sre_destroy_pool(r->pool);
	if (r->mpool)
		sre_destroy_pool(r->mpool);
	free(r);
}

int text_regex_match(Regex *r, const char *data, int eflags) {
	sre_int_t *unused;
	sre_vm_pike_ctx_t *ctx = make_context(r);
	sre_int_t result = sre_vm_pike_exec(ctx, (sre_char*)data, strlen(data), 1, &unused);
	destroy_context(ctx, r);
	return result >= 0 ? 0 : REG_NOMATCH;
}

int text_search_range_forward(Text *txt, size_t pos, size_t len, Regex *r, size_t nmatch, RegexMatch pmatch[], int eflags) {
	int ret = REG_NOMATCH;
	sre_vm_pike_ctx_t *ctx = make_context(r);
	size_t cur = pos, end = pos + len;

	text_iterate(txt, it, pos) {
		len = it.end - it.text;
		size_t next = cur + len;
		if (next > end) {
			len = end - pos;
			next = end;
		}

		sre_int_t *unused;
		sre_int_t result = sre_vm_pike_exec(ctx, (sre_char*)it.text, len,
	                                            next == end ? 1 : 0, &unused);

		if (result == SRE_DECLINED || result == SRE_ERROR) {
			ret = REG_NOMATCH;
			break;
		}

		if (result >= 0) {
			sre_int_t *cap = r->captures;
			for (size_t i = 0; i < nmatch; i++) {
				pmatch[i].start = *cap == -1 ? EPOS : *cap + pos;
				++cap;
				pmatch[i].end = *cap == -1 ? EPOS : *cap + pos;
				++cap;
			}
			ret = 0;
			break;
		}

		cur = next;
	}

	destroy_context(ctx, r);
	return ret;
}

int text_search_range_backward(Text *txt, size_t pos, size_t len, Regex *r, size_t nmatch, RegexMatch pmatch[], int eflags) {
	int ret = REG_NOMATCH;
	size_t end = pos + len;

	while (pos < end && !text_search_range_forward(txt, pos, len, r, nmatch, pmatch, eflags)) {
		ret = 0;
		if (r->captures[0] == 0 && r->captures[1] == 0) {
			size_t next = text_line_next(txt, pos);
			len -= (next - pos);
			pos = next;
		} else {
			len -= r->captures[1];
			pos += r->captures[1];
		}
	}

	return ret;
}
