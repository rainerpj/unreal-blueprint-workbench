"""Session-only settings shared by BA 4.2 and 4.5 runners."""
def override_setting(saved, obj, key, value):
    try:
        previous = obj.get_editor_property(key)
    except Exception as exc:
        # BA 4.2 has no format-after-save feature or corresponding setting.
        if key == 'bFormatAllAfterSaving' and "Failed to find property 'bFormatAllAfterSaving'" in str(exc):
            return
        raise
    saved.append((obj, key, previous))
    obj.set_editor_property(key, value)
