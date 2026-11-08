local text, message = "", "Type a note. ENTER saves."
return {
  init = function() text = storage.load() or "" end,
  key = function(key)
    if key == "enter" then message = storage.save(text) and "Saved to SD card" or "Wait a second, then save again"
    elseif key == "backspace" then text = string.sub(text, 1, math.max(0, #text - 1))
    elseif #key == 1 and #text < 140 then text = text .. key end
  end,
  suspend = function() storage.save(text) end,
  draw = function()
    ui.rect(6, 6, 228, 70, theme.surface)
    for row = 0, 3 do ui.text(12, 13 + row * 15, string.sub(text, row * 35 + 1, row * 35 + 35), 1, theme.text, theme.surface) end
    ui.text(7, 86, message)
  end,
}
