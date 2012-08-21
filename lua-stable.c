#include "stable.h"
#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 16

static void
_getvalue(lua_State *L, int ttype, union table_value *tv) {
	switch (ttype) {
	case ST_NIL:
		lua_pushnil(L);
		break;
	case ST_NUMBER:
		lua_pushnumber(L,tv->n);
		break;
	case ST_BOOLEAN:
		lua_pushboolean(L,tv->b);
		break;
	case ST_ID:
		lua_pushlightuserdata(L,(void *)(uintptr_t)tv->id);
		break;
	case ST_STRING:
		stable_value_string(tv,(table_setstring_func)lua_pushlstring,L);
		break;
	case ST_TABLE:
		lua_pushlightuserdata(L,tv->p);
		break;
	default:
		luaL_error(L,"Invalid stable type %d",ttype);
	}
}

static int
_get(lua_State *L) {
	struct table * t = lua_touserdata(L,1);
	int type = lua_type(L,2);
	int idx;
	int ttype;
	union table_value tv;
	const char *key;
	size_t sz;
	switch(type) {
	case LUA_TNUMBER:
		idx = lua_tointeger(L,2);
		if (idx <= 0) {
			return luaL_error(L,"Unsupport index %d",idx);
		}
		ttype = stable_type(t, NULL, idx - 1, &tv);
		break;
	case LUA_TSTRING:
		key = lua_tolstring(L,2,&sz);
		ttype = stable_type(t, key , sz , &tv);
		break;
	default:
		return luaL_error(L,"Unsupport key type %s",lua_typename(L,type));
	}
	_getvalue(L, ttype, &tv);
	return 1;
}

static void
_error(lua_State *L, const char *key, size_t sz, int type) {
	if (key == NULL) {
		luaL_error(L, "Can't set %d with type %s",(int)sz,lua_typename(L,type));
	} else {
		luaL_error(L, "Can't set %s with type %s",key,lua_typename(L,type));
	}
}

static const char * 
_get_key(lua_State *L, int key_idx, size_t *sz_idx) {
	int type = lua_type(L,key_idx);
	const char *key;
	size_t sz;
	switch(type) {
	case LUA_TNUMBER:
		sz = lua_tointeger(L,key_idx);
		if (sz <= 0) {
			luaL_error(L,"Unsupport index %lu",sz);
		}
		key = NULL;
		sz--;
		*sz_idx = sz;
		break;
	case LUA_TSTRING:
		key = lua_tolstring(L,key_idx,sz_idx);
		break;
	default:
		luaL_error(L,"Unsupport key type %s",lua_typename(L,type));
	}
	return key;
}

static void
_set_value(lua_State *L, struct table * t, const char *key, size_t sz, int idx) {
	int type = lua_type(L,idx);
	int r;
	switch(type) {
	case LUA_TNUMBER: 
		r = stable_setnumber(t, key , sz , lua_tonumber(L,idx));
		break;
	case LUA_TBOOLEAN:
		r = stable_setboolean(t, key, sz, lua_toboolean(L,idx));
		break;
	case LUA_TSTRING: {
		size_t len;
		const char * str = lua_tolstring(L,idx,&len);
		r = stable_setstring(t, key, sz , str, len);
		break;
	}
	case LUA_TLIGHTUSERDATA:
		r = stable_setid(t, key, sz, (uint64_t)(uintptr_t)lua_touserdata(L,idx));
		break;
	default:
		luaL_error(L,"Unsupport value type %s",lua_typename(L,type));
	}
	if (r) {
		_error(L,key,sz,type);
	}
}

static int
_settable(lua_State *L) {
	struct table * t = lua_touserdata(L,1);
	size_t sz;
	const char * key = _get_key(L,2,&sz);
	if (stable_settable(t,key,sz, lua_touserdata(L,3))) {
		_error(L,key,sz,LUA_TLIGHTUSERDATA);
	}
	return 0;
}

static int
_set(lua_State *L) {
	struct table * t = lua_touserdata(L,1);
	size_t sz;
	const char * key = _get_key(L,2,&sz);
	_set_value(L, t, key, sz, 3);
	return 0;
}

static int
_iter_stable_array(lua_State *L) {
	int idx = luaL_checkinteger(L,2);
	lua_pushinteger(L,idx+1);
	union table_value v;
	int t = stable_type(lua_touserdata(L,1), NULL, idx, &v);
	if (t == ST_NIL) {
		return 0;
	}
	_getvalue(L, t, &v);
	return 2;
}

static int
_ipairs(lua_State *L) {
	lua_pushcfunction(L,_iter_stable_array);
	lua_pushvalue(L,1);
	lua_pushinteger(L,0);
	return 3;
}

/*
	uv1:  array_part
	uv2:  hash_part
	uv3:  position
	uv4:  userdata: keys
	lightuserdata: stable
	string key

	string nextkey
	value
 */

static int
_next_key(lua_State *L,struct table *t) {
	int hash_part = lua_tointeger(L,lua_upvalueindex(2));
	if (hash_part == 0)
		return 0;
	int position = lua_tointeger(L,lua_upvalueindex(3));
	if (position >= hash_part) {
		lua_pushinteger(L,0);
		lua_replace(L,lua_upvalueindex(3));
		return 0;
	}
	lua_pushinteger(L,position+1);
	lua_replace(L,lua_upvalueindex(3));

	struct table_key *keys = lua_touserdata(L,lua_upvalueindex(4));
	lua_pushlstring(L,keys[position].key,keys[position].sz_idx);

	union table_value tv;
	int ttype = stable_type(t, keys[position].key,keys[position].sz_idx, &tv);
	_getvalue(L, ttype, &tv);

	return 2;
}

static int
_next_index(lua_State *L,struct table *t, int prev_index) {
	int array_part = lua_tointeger(L,lua_upvalueindex(1));
	if (prev_index >= array_part) {
		return _next_key(L,t);
	}
	int i;
	int ttype;
	union table_value tv;
	for (i=prev_index;i<array_part;i++) {
		ttype = stable_type(t, NULL, i, &tv);
		if (ttype == ST_NIL)
			continue;
		lua_pushinteger(L,i+1);
		_getvalue(L, ttype, &tv);
		return 2;
	}
	return _next_key(L,t);
}

static int
_next_stable(lua_State *L) {
	struct table *t = lua_touserdata(L,1);
	int type = lua_type(L,2);
	switch(type) {
	case LUA_TNIL:
		return _next_index(L,t,0);
		break;
	case LUA_TNUMBER:
		return _next_index(L,t,lua_tointeger(L,2));
	case LUA_TSTRING:
		return _next_key(L,t);
	default:
		return luaL_error(L, "Invalid next key");
	}
}

static int
_pairs(lua_State *L) {
	struct table *t = lua_touserdata(L,1);
	size_t cap = stable_cap(t);
	struct table_key *keys = malloc(cap * sizeof(*keys));
	int size = stable_keys(t,keys,cap);
	if (size == 0) {
		lua_pushinteger(L,0);
		lua_pushinteger(L,0);
		lua_pushinteger(L,0);
		lua_pushnil(L);
	} else {
		int array_part = 0;
		int hash_part = 0;
		int i=0;
		for (i=0;i<size;i++) {
			struct table_key *key = &keys[i];
			if (key->key)
				break;
		}
		if (i==0) {
			hash_part = size;
		} else {
			array_part = keys[i-1].sz_idx + 1;
			hash_part = size - i;
		}
		lua_pushinteger(L,array_part);
		lua_pushinteger(L,hash_part);
		lua_pushinteger(L,0);
		if (hash_part > 0) {
			void * ud = lua_newuserdata(L, hash_part * sizeof(struct table_key));
			memcpy(ud, keys + i, hash_part * sizeof(struct table_key));
		} else {
			lua_pushnil(L);
		}
	}
	lua_pushcclosure(L,_next_stable,4);
	free(keys);
	lua_pushvalue(L,1);
	return 2;
}

static int
_init_mt(lua_State *L) {
	lua_pushlightuserdata(L,NULL);
	int m = lua_getmetatable(L,-1);
	luaL_Reg lib[] = {
		{ "__index", _get },
		{ "__newindex", _set },
		{ "__pairs", _pairs },
		{ "__ipairs", _ipairs },
		{ NULL, NULL },
	};
	if (m == 0) {
		luaL_newlibtable(L,lib);
	}
	luaL_setfuncs(L,lib,0);
	lua_setmetatable(L,-2);
	lua_pop(L,1);

	return 0;
}

static int
_create(lua_State *L) {
	struct table * t = stable_create(L);
	lua_pushlightuserdata(L,t);
	return 1;
}

static int
_release(lua_State *L) {
	struct table ** t = lua_touserdata(L,1);
	stable_release(*t);
	return 0;
}

static int
_grab(lua_State *L) {
	luaL_checktype(L,1,LUA_TLIGHTUSERDATA);
	struct table * t = lua_touserdata(L,1);
	stable_grab(t);
	struct table ** ud = lua_newuserdata(L, sizeof(struct table *));
	*ud = t;
	lua_pushvalue(L,lua_upvalueindex(1));
	lua_setmetatable(L,-2);
	return 1;
}

static int
_decref(lua_State *L) {
	struct table * t = lua_touserdata(L,1);
	stable_release(t);
	return 0;
}

static int
_getref(lua_State *L) {
	struct table * t = lua_touserdata(L,1);
	int ref = stable_getref(t);
	lua_pushinteger(L,ref);
	return 1;
}

static int
_incref(lua_State *L) {
	struct table * t = lua_touserdata(L,1);
	stable_grab(t);
	return 0;
}

int
luaopen_stable_raw(lua_State *L) {
	luaL_checkversion(L);

	luaL_Reg l[] = {
		{ "create" , _create },
		{ "decref", _decref },
		{ "incref", _incref },
		{ "getref", _getref },
		{ "get", _get },
		{ "set", _set },
		{ "settable", _settable },
		{ "pairs", _pairs },
		{ "ipairs", _ipairs },
		{ "init", _init_mt },
		{ NULL, NULL },
	};

	luaL_newlib(L,l);

	lua_createtable(L,0,1);
	lua_pushcfunction(L, _release);
	lua_setfield(L, -2, "__gc"); 
	lua_pushcclosure(L, _grab, 1);
	lua_setfield(L, -2, "grab");

	return 1;
}
