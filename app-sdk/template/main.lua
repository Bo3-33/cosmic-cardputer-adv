local count = 0
return {
  init = function() count = tonumber(storage.load()) or 0 end,
  key = function(key) if key == "enter" then count = count + 1 end end,
  update = function(dt) end,
  draw = function()
    ui.text(12, 16, "Hello COSMIC", 2)
    ui.text(12, 47, "Count: " .. tostring(count))
    ui.text(12, 76, "ENTER Count   TAB Switch app")
  end,
  suspend = function() storage.save(tostring(count)) end,
  resume = function() end,
}
