local c = require "stable.raw"
local assert = assert
local stable_get = c.get
local stable_set = c.set
local stable_inext = c.ipairs()

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

local _struct_meta = {
	__index = function(t,k)
		local it = t.__get
		local index = it[k]
		local enum = it[index]
		local v = stable_get(t.__handle, index)
		if enum then
			local obj = enum[v]
			if obj == nil then
				obj = _bind(v, enum)
				rawset(t,k,obj)
			end
			return obj
		else
			return v
		end
	end,
	__newindex = function(t,k,v)
		local it = t.__set
		local index = it[k]
		local enum = it[index]
		if enum then
			stable_set(t.__handle, index , enum[v])
		elseif type(v) == "table" then
			local sub = stable.create(t.__default[k])
			stable_set(self.__handle, index, sub.__handle)
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
	local k,v = stable_inext(t.__handle , prev)
	if k then
		return k, t[k]
	end
end

local _array_meta = {
	__index = function(t,index)
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
		local enum_set = t.__enum
		if enum_set then
			stable_set(t.__handle, index, enum_set[v])
		else
			local typename = type(v)
			if typename == "table" then
				local sub = stable.create(t.__type)
				stable_set(t.__handle, index, sub.__handle)
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
}

-- local function
function _bind( handle, typeinfo)
	local self = {}
	self.__handle = handle
	self.__iter = typeinfo.iter
	self.__get = typeinfo.get
	self.__set = typeinfo.set
	return setmetatable(self, _struct_meta)
end

local _init_typeinfo	-- function

local function _init_struct(info , src)
	info.iter = {}
	info.get = {}
	info.set = {}
	info.default = {}
	local i = 1
	local anonymous = 0
	for k,v in pairs(src) do
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
				info.get[i] = anonymous_type.name_id
				info.set[i] = anonymous_type.id_name
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
			else
				-- default is typename
				if string.byte(v) == 42 then
					info.default[k] = v
				else
					local typeinfo = _typeinfo[v]
					if typeinfo[1] == "enum" then
						info.get[i] = typeinfo.name_id
						info.set[i] = typeinfo.id_name
						info.default[k] = 1
					else
						info.default[k] = v
					end
				end
			end
		else
			info.default[k] = v
		end

		i = i + 1
	end
end

local function _init_enum(info, src)
	info.name_id = {}
	info.id_name = {}
	for k,v in ipairs(src) do
		info.name_id[k]=v
		info.id_name[v]=k
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

local function _init(self, tinfo)
	for key,default in pairs(tinfo.default) do
		local index = tinfo.get[key]
		if default == "" then
			stable_set(self.__handle, index, "")
		elseif type(default) == "string" then
			local sub = stable.create(default)
			stable_set(self.__handle, index, sub.__handle)
			rawset(self, key , sub)
		else
			stable_set(self.__handle, index, default)
		end
	end
end

function stable.bind( handle , typename)
	return _bind( handle , _typeinfo[typename])
end

function stable.create( typename )
	local self = {}
	self.__handle = c.create()

	if string.byte(typename) == 42 then
		-- '*' == 42 , It's a array
		if typename == "*number" then
			self.__type = "number"
		elseif typename == "*boolean" then
			self.__type = "boolean"
		elseif typename == "*string" then
			self.__type = "string"
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
	self.__iter = _typeinfo[typename].iter
	self.__get = _typeinfo[typename].get
	self.__set = _typeinfo[typename].set
	_init(self, _typeinfo[typename])
	return setmetatable(self, _struct_meta)
end

function stable:release()
	setmetatable(self,nil)
	c.release(self.__handle)
end

return stable
