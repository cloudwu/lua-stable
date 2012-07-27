s = require "stable"
s.init()
a = s.create  {1,2,3,4,nil,5,x=10,y=11}
a.hello = { hello = true, world = false }

local function dump(a, depth)
	for k,v in pairs(a) do
		if type(v) == "userdata" then
			print(string.rep("\t",depth),k,"==>")
			dump(v,depth+1)
		else
			print(string.rep("\t",depth),k,v)
		end
	end
end

dump(a,0)
