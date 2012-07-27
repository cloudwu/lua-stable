## Share table between lua states

```lua
local stable = require "stable"

local t = stable.create()

t.hello = { world = true }

-- you can send t (a lightuserdata) to other lua state. (thread safe)

stable.release(t)

```
