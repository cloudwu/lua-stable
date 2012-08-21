#include "stable.h"
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>

#define MAX_THREAD 32
#define MAX_COUNT 100000

static struct table *
init() {
	struct table * t = stable_create();
	struct table * sub1 = stable_create();
	struct table * sub2 = stable_create();
	stable_settable(t, TKEY("number"),sub1);
	stable_settable(t, TKEY("string"),sub2);
	stable_setnumber(t,TKEY("count"),0);

	return t;
}

static void *
thread_write(void *ptr) {
	struct table * t = ptr;
	int i;
	char buf[32];

	struct table * n = stable_table(t, TKEY("number"));
	struct table * s = stable_table(t, TKEY("string"));

	assert(n && s);

	for (i = 0; i<MAX_COUNT; i++) {
		sprintf(buf,"%d",i);
		stable_setstring(n,TINDEX(i),buf,strlen(buf));
		stable_setnumber(s,buf,strlen(buf),i);
		stable_setnumber(t,TKEY("count"),i+1);
	}

	return NULL;
}

static void
_strto_l(void *ud, const char *str, size_t n) {
	int *r = ud;
	*r = strtol(str,NULL,10);
}

static void *
thread_read(void* ptr) { 
	struct table * t = ptr;
	int last=0;
	struct table * n = stable_table(t, TKEY("number"));
	struct table * s = stable_table(t, TKEY("string"));
	char buf[32];
	while (last != MAX_COUNT) {
		int i = stable_number(t,TKEY("count"));
		if (i == last)
			continue;
		if (i > last+1) {
			printf("%d-%d\n",last,i);
		}
		last = i;
		i--;
		sprintf(buf,"%d",i);
		int v = 0;
		stable_string(n,TINDEX(i),_strto_l,&v);
		assert(v == i);
		double d = stable_number(s,buf,strlen(buf));
		if ((int)d!=i) {
			printf("key = %s i=%d d=%f\n",buf,i,d);
		}
		assert((int)d == i);
	}

	return NULL;
}

static void
test_read(struct table *t) {
	struct table * n = stable_table(t, TKEY("number"));
	struct table * s = stable_table(t, TKEY("string"));
	char buf[32];
	int i;
	for (i=0;i<MAX_COUNT;i++) {
		sprintf(buf,"%d",i);
		int v = 0;
		stable_string(n,TINDEX(i),_strto_l,&v);
		assert(v == i);
		double d = stable_number(s,buf,strlen(buf));
		if ((int)d!=i) {
			printf("key = %s i=%d d=%f\n",buf,i,d);
		}
		assert((int)d == i);
	}
}

int 
main() {
	pthread_t pid[MAX_THREAD];

	struct table *T = init();
	printf("init\n");

	pthread_create(&pid[0], NULL, thread_write, T);

	int i;
	for (i=1;i<MAX_THREAD;i++) {
		pthread_create(&pid[i], NULL, thread_read, T);
	}

	for (i=0;i<MAX_THREAD;i++) {
		pthread_join(pid[i], NULL); 
	}

	printf("main exit\n");

	test_read(T);

	stable_release(T);

	return 0;
}
