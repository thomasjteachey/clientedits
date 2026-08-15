// goeditor.h - in-client gameobject editor (phase 1: cursor->world pick probe).
// See docs/GOEDITOR_CLIENT_RE.md. Reads [GOEditorProbe] from AnimSpeedFix.ini;
// stays OFF unless Enabled=1. Fails safe on any byte-signature mismatch.
#pragma once

void GOEditor_LoadSettings(const char* dir);
void GOEditor_Install();
void GOEditor_Shutdown();   // flush + close the probe log (call on DLL detach)
