local c = require "stable.raw"
local int64 = require "int64"
local assert = assert
local rawget = rawget
local stable_get = assert(c.get)
local stable_set = assert(c.set)
local stable_settable = assert(c.settable)

local stable = {}

--[[
	type info

	{
		type1 = {	-- named struct
			key1 = "typename",	-- object named "typename" , it can be struct or enum
			key2 = "*typename",  -- object array
			key3 = "*number",	-- array number
			key4 = "*boolean",	-- array boolean
			key5 = "number",	-- number , default is 0
			key6 = "boolean",	-- boolean, default is false
			key7 = "string",	-- string, default is ""
			key8 = "", -- string, default is ""
			key9 = 1,	-- number with default value
			key10 = true,	-- boolean with default value
			key11 = {	-- anonymous struct
				key1 = 1,
			}
			key12 = { "ENUM1", "ENUM2" } -- anonymous enum
		},
		type2 = {		-- named enum
			"ENUM1",
			"ENUM2",
		},
	}
]]

local _typeinfo -- table
local _bind	-- function
local _create_node -- function

local function _next(self, key)
	local next_key
	if key == nil then
		next_key = self.__iter[1]
	else
		local prev_key = self.__get[key]
		next_key = self.__iter[prev_key+1]
	end
	if next_key then
		return next_key, self[next_key]
	end
end

local _array_meta

local function _bind_array(self , typename)
	if typename == "*number" then
		self.__type = "number"
	elseif typename == "*boolean" then
		self.__type = "boolean"
	elseif typename == "*string" then
		self.__type = "string"
	elseif typename == "*userdata" then
		self.__type = "userdata"
	else
		typename = string.sub(typename,2)
		local typeinfo = assert(_typeinfo[typename],typename)
		if typeinfo[1] == "enum" then
			self.__type = typeinfo.id_name
			self.__enum = typeinfo.name_id
		else
			self.__type = typename
		end
	end
	return setmetatable(self, _array_meta)
end

local _struct_meta = {
	__index = function(t,k)
		local it = t.__get
		local index = it[k]
		local v = stable_get(t.__handle, index)
		if type(v) == "userdata" then
			local typename = t.__default[k]
			if type(typename) ~= "string" then
				-- It's a int64
				return v
			elseif string.byte(typename) == 42 then	-- '*'
				-- It's a array
				local obj = {}
				obj.__handle = v
				_bind_array(obj, typename)
				rawset(t,k,obj)
				return obj
			else
				local obj = _bind(v, _typeinfo[typename])
				rawset(t,k,obj)
				return obj
			end
		else
			local enum = it[index]
			if enum then
				return enum[v]
			else
				return v
			end
		end
	end,
	__newindex = function(t,k,v)
		local it = t.__set
		local index = it[k]
		local enum = it[index]
		if enum then
			stable_set(t.__handle, index , enum[v])
		elseif type(v) == "table" then
			local sub = _create_node(t.__default[k])
			stable_settable(self.__handle, index, sub.__handle)
			rawset(self, key , sub)
			for k,v in pairs(v) do
				sub[k] = v
			end
		else
			stable_set(t.__handle, index , v)
		end
	end,
	__pairs = function(t)
		return _next, t
	end
}

local function _next_array(t,prev)
	local n = stable_get(t.__handle, 's') + 1
	local index = prev + 1
	if index < n then
		return index,t[index]
	end
end

-- _array_meta is local
_array_meta = {
	__index = function(t,index)
		local n = stable_get(t.__handle, 's') + 1
		if index >= n then
			return nil
		end
		local obj = stable_get(t.__handle, index)
		if obj then
			local typename = t.__type
			local typeinfo = _typeinfo[typename]

			if typeinfo then
				obj = _bind(obj, typeinfo)
				rawset(t,index,obj)
				return obj
			elseif type(typename) == "table" then
				-- It's a enum
				return typename[obj]
			end
		end
		return obj
	end,
	__newindex = function(t,index,v)
		local n = stable_get(t.__handle, 's') + 1
		if index == n then
			stable_set(t.__handle,'s',n)
		elseif index > n then
			stable.resize(t, index)
		end
		local enum_set = rawget(t,"__enum")
		if enum_set then
			stable_settable(t.__handle, index, enum_set[v])
		else
			local typename = type(v)
			if typename == "table" then
				local sub = _create_node(t.__type)
				stable_settable(t.__handle, index, sub.__handle)
				rawset(t, index , sub)
				for k,v in pairs(v) do
					sub[k] = v
				end
			else
				assert(typename == t.__type)
				stable_set(t.__handle, index, v)
			end
		end
	end,
	__pairs = function(t)
		return _next_array , t , 0
	end,
	__ipairs = function(t)
		return _next_array , t , 0
	end,
	__len = function(t)
		return stable_get(t.__handle , 's')
	end,
}

-- local function
function _bind( handle, typeinfo)
	local self = {}
	self.__handle = handle
	self.__iter = typeinfo.iter
	self.__get = typeinfo.get
	self.__set = typeinfo.set
	self.__default = typeinfo.default
	return setmetatable(self, _struct_meta)
end

local function gen_index(tbl)
	local index = {}
	for k in pairs(tbl) do
		table.insert(index,k)
	end
	table.sort(index)
	local ret = {}
	for k,v in ipairs(index) do
		ret[v] = k
	end
	return ret
end

local _init_typeinfo	-- function

local function _init_struct(info , src)
	info.iter = {}
	info.get = {}
	info.set = {}
	info.default = {}
	local anonymous = 0
	local index_table = gen_index(src)
	for k,v in pairs(src) do
		local i = index_table[k]
		info.iter[i] = k
		info.get[k] = i
		info.set[k] = i
		local t = type(v)
		if t == "table" then
			-- anonymous struct or enum
			local anonymous_name = "___anonymous__" .. tostring(anonymous)
			local anonymous_type = {}
			_typeinfo[anonymous_name] = anonymous_type
			_init_typeinfo(anonymous_type , v)
			if v[1] then
				-- It's an enum
				info.set[i] = anonymous_type.name_id
				info.get[i] = anonymous_type.id_name
				info.default[k] = 1
			else
				-- It's a struct
				info.default[k] = anonymous_name
			end
			anonymous = anonymous + 1
		elseif t == "string" then
			if v == "number" then
				info.default[k] = 0
			elseif v == "boolean" then
				info.default[k] = false
			elseif v == "string" or v == "" then
				info.default[k] = ""
			elseif v == "userdata" then
				info.default[k] = int64.new(0)
			else
				-- default is typename
				local fc = string.byte(v)
				if fc == 42 or fc == 46 then	-- '*' or '.'
					info.default[k] = v
				else
					local typeinfo = _typeinfo[v]
					if typeinfo[1] == "enum" then
						info.set[i] = typeinfo.name_id
						info.get[i] = typeinfo.id_name
						info.default[k] = 1
					else
						info.default[k] = v
					end
				end
			end
		else
			info.default[k] = v
		end
	end
end

local function _init_enum(info, src)
	info.name_id = {}
	info.id_name = {}
	for k,v in ipairs(src) do
		info.name_id[v]=k
		info.id_name[k]=v
	end
end

-- local function defined
function _init_typeinfo(info, src)
	if src[1] then
		info[1] = "enum"
		_init_enum(info,src)
	else
		info[1] = "struct"
		_init_struct(info,src)
	end
end

function stable.init(typeinfo)
	_typeinfo = {}
	for k,v in pairs(typeinfo) do
		if v[1] then
			_typeinfo[k] = { "enum" }
			_init_enum(_typeinfo[k],v)
		else
			_typeinfo[k] = { "struct" }
		end
	end
	for k,v in pairs(typeinfo) do
		if _typeinfo[k][1] == "struct" then
			_init_struct(_typeinfo[k],v)
		end
	end

	return _typeinfo
end

local function _init(self, typename)
	local tinfo = _typeinfo[typename]
	for key,default in pairs(tinfo.default) do
		local index = tinfo.get[key]
		if type(default) == "string" then
			if string.byte(default) == 46 then -- '.'
				stable_set(self.__handle, index, string.sub(default,2))
			elseif default == "" then
				stable_set(self.__handle, index, default)
			else
				local sub = _create_node(default)
				stable_settable(self.__handle, index, sub.__handle)
				rawset(self, key , sub)
			end
		else
			stable_set(self.__handle, index, default)
		end
	end
end

function stable.bind( handle , typename)
	local self = _bind( handle , _typeinfo[typename])
	local gcobj = c.grab(handle)
	rawset(self, "__gc" , gcobj)
	return self
end

-- create_node is local
function _create_node(typename)
	local self = {}
	self.__handle = c.create()

	if string.byte(typename) == 42 then
		-- '*' == 42 , It's a array
		stable_set(self.__handle, 's' , 0)
		return _bind_array(self, typename)
	end
	self.__iter = _typeinfo[typename].iter
	self.__get = _typeinfo[typename].get
	self.__set = _typeinfo[typename].set
	self.__default = _typeinfo[typename].default

	_init(self, typename)
	return setmetatable(self, _struct_meta)
end

function stable.create(typename)
	local self = _create_node( typename )
	local gcobj = c.grab(self.__handle)
	c.decref(self.__handle)
	rawset(self,"__gc", gcobj)
	return self
end

local _default_value = {
	number = 0,
	boolean = false,
	string = "",
	userdata = int64.new(0),
}

local function _reset_default(self,typename)
	if string.byte(typename) == 42 then
		stable.resize(self,0)
		return
	end
	local tinfo = _typeinfo[typename]
	for key,default in pairs(tinfo.default) do
		local index = tinfo.get[key]
		if type(default) == "string" then
			if string.byte(default) == 46 then
				stable_set(self.__handle, index, string.sub(default,2))
			elseif default == "" then
				stable_set(self.__handle, index, default)
			else
				_reset_default(self[key] , default)
			end
		else
			stable_set(self.__handle, index, default)
		end
	end
end

function stable.resize(t,size)
	local n = assert(stable_get(t.__handle,'s'))
	stable_set(t.__handle,'s',size)
	if size > n then
		local typename = t.__type
		local default
		if type(typename) == "table" then
			-- enum
			default = 1
		else
			default = _default_value[typename]
		end
		if default then
			for i = n+1,size do
				stable_set(t.__handle, i , default)
			end
		else
			for i = n+1,size do
				t[i] = {}
			end
		end
	else
		for i = size+1, n do
			rawset(t,i,nil)
		end
	end
end

return stable
