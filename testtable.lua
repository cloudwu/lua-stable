local print_r = require "print_r"
local stable = require "stable"

local info = stable.init {
	foo = {
		bars = "*bar",	-- struct bar array
		enums = "*xx",
	},
	bar = {
		first = true,
		second = 1,
		third = "*xx", -- enum xx array
	},
	xx = { "ONE", "TWO" }
}

local a = stable.create "foo"

for i=1,5 do
	a.enums[i] = "TWO"
end

for i=1,#a.enums do
	print(a.enums[i])
end

stable.resize(a.enums,1)
stable.resize(a.enums,3)

for k,v in ipairs(a.enums) do
	print(k,v)
end

stable.resize(a.bars,2)
a.bars[2].second = 2
stable.resize(a.bars[2].third,3)
a.bars[2].third[2] = "TWO"

stable.resize(a.bars,1)

print_r(a)
