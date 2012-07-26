#include "stable.h"
#include <inttypes.h>
#include <stdio.h>
#include <assert.h>

static void
print_string(void *ud, const char *string, size_t sz) {
	printf("%s",string);
}

static void
dump(struct table *root, int depth) {
	size_t size = stable_cap(root);
	struct table_key keys[size];
	size = stable_keys(root,keys,size);
	int i;
	for (i=0;i<depth;i++)
		printf("  ");
	for (i=0;i<size;i++) {
		if (keys[i].key == NULL) {
			printf("[%d] = ",keys[i].sz_idx);
		} else {
			printf("%s = ",keys[i].key);
		}
		switch(keys[i].type) {
		case ST_NIL:
			printf("nil");
			break;
		case ST_NUMBER: {
			double d = stable_number(root, keys[i].key, keys[i].sz_idx);
			printf("%lf",d);
			break;
		}
		case ST_BOOLEAN: {
			int b = stable_boolean(root, keys[i].key, keys[i].sz_idx);
			printf("%s",b ? "true" : "false");
			break;
		}
		case ST_ID: {
			uint64_t id = stable_id(root, keys[i].key, keys[i].sz_idx);
			printf("%" PRIu64,id);
			break;
		}
		case ST_STRING:
			stable_string(root,keys[i].key, keys[i].sz_idx, print_string, NULL);
			break;
		case ST_TABLE: {
			struct table * sub = stable_table(root, keys[i].key, keys[i].sz_idx);
			printf(":\n");
			dump(sub,depth+1);
			break;
		}
		default:
			assert(0);
			break;
		}
		printf("\n");
	}
}

static void
test(struct table *root) {
	struct table * sub = stable_settable(root,TKEY("hello"));
	stable_setnumber(root,TINDEX(10),100);
	stable_setstring(sub,TINDEX(0),TKEY("world"));
	dump(root,0);
}

int
main() {
	struct table * t = stable_create();
	test(t);
	stable_release(t);
	return 0;
}
