local print_r = require "print_r"
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
stable.resize(a.bars,1)
print_r(a)
a.bars[2] = { third = { "ONE" } }
print("=======")
print_r(a)
