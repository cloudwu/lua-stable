#include "stable.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#define DEFAULT_SIZE 4
#define MAX_HASH_DEPTH 3
#define MAGIC_NUMBER 0x5437ab1e

/*
static int MEM = 0;

static void *my_malloc(size_t sz) {
	__sync_add_and_fetch (&MEM,1);
	return malloc(sz);
}
static void my_free(void *p) {
	__sync_sub_and_fetch (&MEM,1);
	free(p);
}

#define malloc my_malloc
#define free my_free
*/

struct map;
struct array;

struct string_slot {
	int ref;
	int sz;
	char buf[1];
};

struct string {
	int lock;
	struct string_slot *slot;
};

struct table {
	int ref;
	int magic;
	int lock;
	int map_lock;
	int array_lock;
	struct map *map;
	struct array *array;
};

struct value {
	int type;
	union {
		double n;
		int b;
		uint64_t id;
		struct table *t;
		struct string *s;
	} v;
};

struct node {
	struct node *next;
	struct string_slot *k;
	struct value v;
};

struct map {
	int ref;
	int size;
	struct node *n[1];
};

struct array {
	int ref;
	int size;
	struct value a[1];
};

static inline void
_table_lock(struct table *t) {
	while (__sync_lock_test_and_set(&t->lock, 1)) {}
}

static inline void
_table_unlock(struct table *t) {
	__sync_lock_release(&t->lock);
}

static inline struct string_slot *
_grab_string(struct string *s) {
	while (__sync_lock_test_and_set(&s->lock, 1)) {}
		int ref = __sync_add_and_fetch(&s->slot->ref,1);
		assert(ref > 1);
		struct string_slot * ret = s->slot;
	__sync_lock_release(&s->lock);
	return ret;
}

static inline void
_release_string(struct string_slot *s) {
	if (__sync_sub_and_fetch(&s->ref,1) == 0) {
		free(s);
	}
}

static inline struct string_slot *
new_string(const char *name, size_t sz) {
	struct string_slot *s = malloc(sizeof(*s) + sz);
	s->ref = 1;
	s->sz = sz;
	memcpy(s->buf, name, sz);
	s->buf[sz] = '\0';
	return s;
}

static inline void
_update_string(struct string *s, const char *name, size_t sz) {
	struct string_slot * ns = new_string(name,sz);
	while (__sync_lock_test_and_set(&s->lock, 1)) {}
		struct string_slot * old = s->slot;
		s->slot = ns;
		int ref = __sync_sub_and_fetch(&old->ref,1);
	__sync_lock_release(&s->lock);
	if (ref == 0) {
		free(old);
	}
}

static inline struct array *
_grab_array(struct table *t) {
	while (__sync_lock_test_and_set(&t->array_lock, 1)) {}
		int ref = __sync_add_and_fetch(&t->array->ref,1);
		assert(ref > 1);
		struct array * ret = t->array;
	__sync_lock_release(&t->array_lock);
	return ret;
}

static inline void
_release_array(struct array *a) {
	if (__sync_sub_and_fetch(&a->ref,1) == 0) {
		free(a);
	}
}

static inline void
_update_array(struct table *t, struct array *a) {
	while (__sync_lock_test_and_set(&t->array_lock, 1)) {}
		struct array *old = t->array;
		t->array = a;
		int ref = __sync_sub_and_fetch(&old->ref,1);
	__sync_lock_release(&t->array_lock);
	if (ref == 0) {
		free(old);
	}
}

static inline struct map *
_grab_map(struct table *t) {
	while (__sync_lock_test_and_set(&t->map_lock, 1)) {}
		int ref = __sync_add_and_fetch(&t->map->ref,1);
		assert(ref > 1);
		struct map * ret = t->map;
	__sync_lock_release(&t->map_lock);
	return ret;
}

static void
_delete_map_without_data(struct map *m) {
	int i;
	for (i=0;i<m->size;i++) {
		struct node * n = m->n[i];
		while(n) {
			struct node * next = n->next;
			free(n);
			n = next;
		}
	}
	free(m);
}

static inline void
_release_map(struct map *m) {
	if (__sync_sub_and_fetch(&m->ref,1) == 0) {
		_delete_map_without_data(m);
	}
}

static inline void
_update_map(struct table *t, struct map *m) {
	while (__sync_lock_test_and_set(&t->map_lock, 1)) {}
		struct map * old = t->map;
		t->map = m;
		int ref = __sync_sub_and_fetch(&old->ref,1);
	__sync_lock_release(&t->map_lock);
	if (ref == 0) {
		_delete_map_without_data(old);
	}
}

struct table *
stable_create() {
	struct table * t = malloc(sizeof(*t));
	memset(t,0,sizeof(*t));
	t->ref = 1;
	t->magic = MAGIC_NUMBER;
	return t;
};

void 
stable_grab(struct table * t) {
	__sync_add_and_fetch(&t->ref, 1);
}

static void
_clear_value(struct value *v) {
	switch(v->type) {
	case ST_STRING:
		free(v->v.s->slot);
		free(v->v.s);
		break;
	case ST_TABLE:
		stable_release(v->v.t);
		break;
	}
}

static void
_delete_array(struct array *a) {
	assert(a->ref == 1);
	int i;
	for (i=0;i<a->size;i++) {
		struct value *v = &a->a[i];
		_clear_value(v);
	}
	free(a);
}

static void
_delete_map(struct map *m) {
	assert(m->ref == 1);
	int i;
	for (i=0;i<m->size;i++) {
		struct node * n = m->n[i];
		while(n) {
			struct node * next = n->next;
			free(n->k);
			_clear_value(&n->v);
			free(n);
			n = next;
		}
	}
	free(m);
}

int
stable_getref(struct table *t) {
	return t->ref;
}

void 
stable_release(struct table *t) {
	if (t) {
		if (__sync_sub_and_fetch(&t->ref,1) != 0) {
			return;
		}
		if (t->array) {
			_delete_array(t->array);
		}
		if (t->map) {
			_delete_map(t->map);
		}
		t->magic = 0;
		free(t);
//		printf("memory = %d\n",MEM);
	}
}

static struct array *
_create_array(size_t n) {
	struct array *a;
	size_t sz = sizeof(*a) + (n-1) * sizeof(struct value);
	a = malloc(sz);
	memset(a,0,sz);
	a->ref = 1;
	a->size = n;
	return a;
}

static struct array *
_init_array(struct table *t, int cap) {
	int size = DEFAULT_SIZE;
	while (cap >= size) {
		size *=2;
	}
	struct array *a = _create_array(size);
	t->array = a;
	return a;
}

static struct map *
_create_hash(size_t n) {
	struct map *m;
	size_t sz = sizeof(*m) + (n-1) * sizeof(struct node *);
	m = malloc(sz);
	memset(m,0,sz);
	m->ref = 1;
	m->size = n;
	return m;
}

static struct map *
_init_map(struct table *t) {
	struct map *m = _create_hash(DEFAULT_SIZE);
	t->map = m;
	return m;
}

static void
_search_array(struct table *t, size_t idx, struct value *result) {
	struct array *a;
	do {
		a = _grab_array(t);
		if (idx < a->size) {
			*result = a->a[idx];
		} else {
			result->type = ST_NIL;
		}
		_release_array(a);
	} while(a!=t->array);
}

static inline uint32_t
hash(const char *name,size_t len) {
	uint32_t h=(uint32_t)len;
	size_t i;
	for (i=0; i<len; i++)
	    h = h ^ ((h<<5)+(h>>2)+(uint32_t)name[i]);
	return h;
}

static inline int 
cmp_string(struct string_slot * a, const char * b, size_t sz) {
	return a->sz == sz && memcmp(a->buf, b, sz) == 0;
}

static void
_search_map(struct table *t, const char *key, size_t sz, struct value *result) {
	uint32_t h = hash(key,sz);
	struct map *m;
	do {
		m = _grab_map(t);

		struct node *n = m->n[h & (m->size-1)];
		
		while (n) {
			if (cmp_string(n->k,key,sz)) {
				*result = n->v;
				break;
			}
			n=n->next;
		}
		if (n == NULL) {
			result->type = ST_NIL;
		}

		_release_map(m);

	} while(m!=t->map);
}

static void
_search_table(struct table *t, const char *key, size_t sz_idx, struct value * result) {
	if (key == NULL) {
		if (t->array) {
			_search_array(t,sz_idx, result);
		} else {
			result->type = ST_NIL;
		}
	} else {
		if (t->map) {
			_search_map(t,key,sz_idx,result);
		} else {
			result->type = ST_NIL;
		}
	}
}

int 
stable_type(struct table *t, const char *key, size_t sz_idx, union table_value *v) {
	struct value tmp;
	_search_table(t,key,sz_idx,&tmp);
	if (v) {
		memcpy(v,&tmp.v,sizeof(*v));
	}
	return tmp.type;
}

double 
stable_number(struct table *t, const char *key, size_t sz_idx) {
	struct value tmp;
	_search_table(t,key,sz_idx,&tmp);
	assert(tmp.type == ST_NIL || tmp.type == ST_NUMBER);
	return tmp.v.n;
}

int 
stable_boolean(struct table *t, const char *key, size_t sz_idx) {
	struct value tmp;
	_search_table(t,key,sz_idx,&tmp);
	assert(tmp.type == ST_NIL || tmp.type == ST_BOOLEAN);
	return tmp.v.b;
}

uint64_t 
stable_id(struct table *t, const char *key, size_t sz_idx) {
	struct value tmp;
	_search_table(t,key,sz_idx,&tmp);
	assert(tmp.type == ST_NIL || tmp.type == ST_ID);
	return tmp.v.id;
}

void
stable_string(struct table *t, const char *key, size_t sz_idx, void (*sfunc)(void *ud, const char *str, size_t sz), void *ud) {
	struct value tmp;
	_search_table(t,key,sz_idx,&tmp);
	if (tmp.type == ST_STRING) {
		struct string_slot *s = _grab_string(tmp.v.s);
		sfunc(ud,s->buf,s->sz);
		_release_string(s);
	} else {
		assert(tmp.type == ST_NIL);
		sfunc(ud,"",0);
	}
}

void
stable_value_string(union table_value *v, void (*sfunc)(void *ud, const char *str, size_t sz), void *ud) {
	struct string_slot *s = _grab_string(v->p);
	sfunc(ud,s->buf,s->sz);
	_release_string(s);
}

static struct array *
_expand_array(struct table *t, size_t idx) {
	struct array * old = t->array;
	int sz = old->size;
	while (sz <= idx) {
		sz *= 2;
	}

	struct array * a = _create_array(sz);
	memcpy(a->a, old->a, old->size * sizeof(struct value));

	_update_array(t, a);

	return a;
}

static void
_insert_hash(struct map *m, struct string_slot *k, struct value *v) {
	uint32_t h = hash(k->buf,k->sz);
	struct node **pn = &m->n[h & (m->size-1)];
	struct node * n = malloc(sizeof(*n));
	n->next = *pn;
	n->k = k;
	n->v = *v;

	*pn = n;
}

static void 
_expand_hash(struct table *t) {
	struct map * old = t->map;
	struct map * m = _create_hash(old->size * 2);

	int i;
	for (i=0;i<old->size;i++) {
		struct node * n = old->n[i];
		while (n) {
			struct node * next = n->next;
			_insert_hash(m, n->k, &n->v);
			n = next;
		}
	}

	_update_map(t,m);
}

static struct node *
_new_node(const char *key, size_t sz, int type) {
	struct node * n = malloc(sizeof(*n));
	n->next = NULL;
	n->k = new_string(key,sz);
	n->v.type = type;
	return n;
}

struct table * 
stable_table(struct table *t, const char *key, size_t sz_idx) {
	struct value tmp;
	_search_table(t,key,sz_idx,&tmp);
	if (tmp.type == ST_TABLE) {
		return tmp.v.t;
	} 
	assert(tmp.type == ST_NIL);
	return NULL;
}

static int
_insert_array_value(struct table *t, size_t idx, struct value *v) {
	struct array *a = t->array;
	if (a == NULL) {
		a = _init_array(t,idx);
	} else {
		if (idx >= a->size) {
			a = _expand_array(t,idx);
		}
	}
	int type = a->a[idx].type;
	a->a[idx] = *v;
	return type;
}

static int
_insert_map_value(struct table *t, const char *key, size_t sz, struct value *v) {
	struct map *m = t->map;
	if (m == NULL) {
		m = _init_map(t);
	}
	uint32_t h = hash(key,sz);
	struct node **pn = &m->n[h & (m->size-1)];
	int depth = 0;
	while (*pn) {
		struct node *tmp = *pn;
		if (cmp_string(tmp->k, key, sz)) {
			int type = tmp->v.type;
			tmp->v.v = v->v;
			return type;
		}
		pn = &tmp->next;
		++depth;
	}

	struct node * n = _new_node(key,sz,v->type);
	n->v = *v;
	*pn = n;

	if (depth > MAX_HASH_DEPTH) {
		_expand_hash(t);
	}

	return ST_NIL;
}

static inline int
_insert_table(struct table *t, const char *key, size_t sz_idx, struct value *v) {
	_table_lock(t);
	int type;
	if (key == NULL) {
		type = _insert_array_value(t, sz_idx , v);
	} else {
		type = _insert_map_value(t,key,sz_idx, v);
	}
	_table_unlock(t);
	return type;
}

int
stable_settable(struct table *t, const char *key, size_t sz_idx, struct table * sub) {
	struct value tmp;
	_search_table(t,key,sz_idx,&tmp);
	if (tmp.type == ST_TABLE) {
		stable_release(tmp.v.t);
	}

	tmp.type = ST_TABLE;
	tmp.v.t = sub;
	int type = _insert_table(t,key,sz_idx,&tmp);
	if (type != ST_NIL && type!= ST_TABLE) {
		return 1;
	}
	return 0;
}

int
stable_setnumber(struct table *t, const char *key, size_t sz_idx, double n) {
	struct value tmp;
	tmp.type = ST_NUMBER;
	tmp.v.n = n;
	int type = _insert_table(t,key,sz_idx,&tmp);
	return type != ST_NUMBER && type != ST_NIL;
}

int
stable_setboolean(struct table *t, const char *key, size_t sz_idx, int b) {
	struct value tmp;
	tmp.type = ST_BOOLEAN;
	tmp.v.b = b;
	int type = _insert_table(t,key,sz_idx,&tmp);
	return type != ST_BOOLEAN && type != ST_NIL;
}

int
stable_setid(struct table *t, const char *key, size_t sz_idx, uint64_t id) {
	struct value tmp;
	tmp.type = ST_ID;
	tmp.v.id = id;
	int type = _insert_table(t,key,sz_idx,&tmp);
	return type != ST_ID && type != ST_NIL;
}

int
stable_setstring(struct table *t, const char *key, size_t sz_idx, const char * str, size_t sz) {
	struct value tmp;
	_search_table(t,key,sz_idx,&tmp);
	if (tmp.type == ST_STRING) {
		_update_string(tmp.v.s, str, sz);
		return 0;
	}
	if (tmp.type != ST_NIL) {
		return 1;
	}
	struct string * s = malloc(sizeof(*s));
	memset(s,0,sizeof(*s));
	s->slot = new_string(str,sz);
	tmp.type = ST_STRING;
	tmp.v.s = s;
	_insert_table(t,key,sz_idx,&tmp);

	return 0;
}

size_t 
stable_cap(struct table *t) {
	size_t s = 0;
	if (t->array) {
		struct array * a = _grab_array(t);
		s += a->size;
		_release_array(a);
	}
	if (t->map) {
		struct map * m = _grab_map(t);
		if (m) {
			s += m->size * MAX_HASH_DEPTH;
		}
		_release_map(m);
	}
	return s;
}

size_t 
stable_keys(struct table *t, struct table_key *vv, size_t cap) {
	size_t count = 0;
	if (t->array) {
		struct array * a = _grab_array(t);
		int i;
		for (i=0;i<a->size;i++) {
			if (count>=cap) {
				_release_array(a);
				return count;
			}
			struct value *v = &(a->a[i]);
			if (v->type == ST_NIL) {
				continue;
			}
			vv[count].type = v->type;
			vv[count].key = NULL;
			vv[count].sz_idx = i;
			++count;
		}
		_release_array(a);
	}
	if (t->map) {
		struct map * m = _grab_map(t);
		int i;
		for (i=0;i<m->size;i++) {
			struct node * n =  m->n[i];
			while (n) {
				if (count>=cap) {
					_release_map(m);
					return count;
				}
				vv[count].type = n->v.type;
				vv[count].key = n->k->buf;
				vv[count].sz_idx = n->k->sz;
				++count;
				n=n->next;
			}
		}
		_release_map(m);
	}
	return count;
}
