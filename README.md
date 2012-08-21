## Share table between lua states

### lua api with meta info support
```lua
local stable = require "stable"

-- init meta info first , we have two struct and one enum now : "foo" , "bar" and "xx"
local info = stable.init {
	foo = {
		hello = 1,	-- number , default value is 1
		world = { "Alice", "Bob" },	-- anonymous enum
		foobar = {
			foobar = "",	-- string
		},
		array = "*number",	-- number array
		bars = "*bar",	-- struct bar array
	},
	bar = {
		first = true,
		second = 1,
		third = "*xx", -- enum xx array
	},
	xx = { "ONE", "TWO" }
}

-- create an object with type "foo"
a = stable.create "foo"

a.world = "Alice"
a.foobar = "xxx"
a.bars[1] = { second = 2 }
a.bars[2] = { third = { "ONE" , "ONE" , "TWO" } }


```

### raw api
```lua
local sraw = require "stable.raw"

sraw.init()  -- init lightuserdata metatable

local t = sraw.create()

t.hello = { world = true } 
-- If you don't want to set explicit, use this :
--   sraw.set(t,"hello", { world = true })

print(t.hello.world)
-- or use this :
--   hello = sraw.get(t,"hello") ; print( sraw.get(hello, "world"))

-- you can send t (a lightuserdata) to other lua state. (thread safe)

for k,v in pairs(t.hello) do  -- or use sraw.pairs
  print (k,v)
end

```
