local angle, speed = 0, 1
return {
  key = function(key)
    if key == "right" then speed = math.min(4, speed + 0.25) end
    if key == "left" then speed = math.max(0, speed - 0.25) end
  end,
  update = function(dt) app.redraw(); angle = (angle + dt * speed) % (math.pi * 2) end,
  draw = function()
    ui.text(9, 8, "A tiny COSMIC app", 1, theme.accent)
    for i = 0, 23 do
      local a = i / 24 * math.pi * 2
      ui.rect(math.floor(120 + math.cos(a) * 70), math.floor(55 + math.sin(a) * 24), 2, 2, theme.panel)
    end
    ui.rect(116, 51, 8, 8, theme.text)
    ui.rect(math.floor(117 + math.cos(angle) * 70), math.floor(52 + math.sin(angle) * 24), 6, 6, theme.accent)
    ui.text(9, 87, "LEFT/RIGHT Speed   " .. tostring(speed))
  end,
}
