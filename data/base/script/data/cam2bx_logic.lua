-- Generated by wz2lua (implementation file)
version(0) --- version of the script API this script is written for

function start()
	addPower(player, power)
	playSound(powTransSnd, player)
end
delayedEvent(start, 4.0)


---------- stubs ----------

if playSound == nil then playSound = function() print("stub: playSound"); return 0 end end
if addPower == nil then addPower = function() print("stub: addPower"); return 0 end end