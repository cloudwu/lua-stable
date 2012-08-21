#ifndef SHARE_TABLE_H
#define SHARE_TABLE_H

#include <stdint.h>
#include <stddef.h>

#define ST_NIL 0
#define ST_NUMBER 1
#define ST_BOOLEAN 2
#define ST_ID 3
#define ST_STRING 4
#define ST_TABLE 5

struct table_key {
	int type;
	const char *key;
	size_t sz_idx;
};

union table_value {
	double n;
	int b;
	uint64_t id;
	void *p;
};

struct table;

typedef void (*table_setstring_func)(void *ud, const char *str, size_t sz);

struct table * stable_create();
void stable_grab(struct table *);
int stable_getref(struct table *);
void stable_release(struct table *);

#define TKEY(x) x,sizeof(x)
#define TINDEX(x) NULL,x

double stable_number(struct table *, const char *key, size_t sz_idx);
int stable_boolean(struct table *, const char *key, size_t sz_idx);
uint64_t stable_id(struct table *, const char *key, size_t sz_idx);
void stable_string(struct table *, const char *key, size_t sz_idx, table_setstring_func sfunc, void *ud);
struct table * stable_table(struct table *, const char *key, size_t sz_idx);

int stable_type(struct table *, const char *key, size_t sz_idx, union table_value *v);
void stable_value_string(union table_value *v, table_setstring_func sfunc, void *ud);

struct table * stable_settable(struct table *, const char *key, size_t sz_idx);
int stable_setnumber(struct table *, const char *key, size_t sz_idx, double n);
int stable_setboolean(struct table *, const char *key, size_t sz_idx, int b);
int stable_setid(struct table *, const char *key, size_t sz_idx, uint64_t id);
int stable_setstring(struct table *, const char *key, size_t sz_idx, const char * str, size_t sz);

size_t stable_cap(struct table *);
size_t stable_keys(struct table *, struct table_key *v, size_t cap);

#endif
