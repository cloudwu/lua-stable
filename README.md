## Share table between lua states

```lua
local stable = require "stable"

stable.init()  -- init lightuserdata metatable

local t = stable.create()

t.hello = { world = true } 
-- If you don't want to set explicit, use this :
--   stable.set(t,"hello", { world = true })

print(t.hello.world)
-- or use this :
--   hello = stable.get(t,"hello") ; print( stable.get(hello, "world"))

-- you can send t (a lightuserdata) to other lua state. (thread safe)

for k,v in pairs(t.hello) do  -- or use stable.pairs
  print (k,v)
end

stable.release(t)

```
