local elapsed, ticking = 0, false
return {
  key = function(key)
    if key == "enter" then ticking = not ticking end
    if key == "r" then elapsed = 0; ticking = false end
  end,
  update = function(dt) if ticking then elapsed = elapsed + dt end end,
  draw = function()
    ui.text(12, 9, ticking and "RUNNING" or "PAUSED", 1, theme.accent)
    ui.text(12, 30, string.format("%02d:%04.1f", math.floor(elapsed / 60), elapsed % 60), 3)
    ui.rect(12, 67, 216, 1, theme.panel)
    ui.text(12, 81, "ENTER Start/stop   R Reset")
  end,
}
