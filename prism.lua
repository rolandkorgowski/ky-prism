-- Only ky-prism: preserve photograph opacity; compositor blur affects alpha chrome.
o.window("^(ky-prism|omarchy-prism|Prism)$", {
  opacity = "1.0 override 1.0 override 1.0 override",
  no_blur = false,
  float = true,
  center = true,
})
