#pragma once
// HSP3 file-event adapter: routes every HSP file command through GameFileSystem
// without modifying the HSP core.
//
// Mechanism (OpenHSP hsp3code.cpp code_event/call_eventfunc): for each type id
// >= HSP3_TYPE_USER (18) whose HSP3TYPEINFO.option has HSPEVENT_ENABLE_FILE,
// eventfunc(event, prm1, prm2, prm3) is called first; a nonzero return skips the
// core's own fopen-based code. This adapter returns 1 for every file event it
// receives (FNAME, FREAD, FWRITE, FEXIST, FDELETE, FMKDIR, FCHDIR, FCOPY,
// FDIRLIST1, FDIRLIST2) and fails with `throw HSPERR_FILE_IO` (the same error
// the core raises), so no file command can fall through to the host OS.
//
// Commands covered: exist, delete, mkdir, chdir (rejected), dirlist, bload,
// bsave, bcopy, noteload, notesave. NOT covered (separate adapters must call
// GameFileSystem::resolve_read/read_file): picload/getpicsize (PICLOAD events),
// mmload/media, plugin functions (z.hpi zOpen, hspext, gf*), memfile/chdpm,
// and dirinfo(1)/dir_exe, which must return GameFileSystem::virtual_root().
//
// Host integration (one place):
//   // after hsp->Reset() has registered the AX's own #regcmd plugin types
//   // (z.hpi / water.hpi occupy fixed ids 18+; do not take an id before them)
//   HSP3TYPEINFO* info = code_gettypeinfo(-1);
//   elona::platform::hsp::install_file_events(info, &hsp->hspctx, files.get());
#include "platform/game_files.h"

// HSPCTX / HSP3TYPEINFO are anonymous-struct typedefs: include the core header
// (the host build already has the OpenHSP src/hsp3 include directory).
#include "hsp3struct.h"

namespace elona::platform::hsp {

// Sets info->option |= HSPEVENT_ENABLE_FILE and info->eventfunc. `info` must be
// a fresh user type (eventfunc == nullptr) or install fails (returns false).
// The adapter keeps raw pointers: ctx and files must outlive the VM.
bool install_file_events(HSP3TYPEINFO* info, HSPCTX* ctx, GameFileSystem* files);

// Raw eventfunc (exposed for hosts that chain several event handlers and for
// tests). Returns 1 for file events, 0 for any other event id.
int file_event(int event, int prm1, int prm2, void* prm3);

// Diagnostic of the most recent failure (UTF-8 detail with the HSP path and
// cause) so the host's error dialog/log can show more than "File I/O error".
const FileDiagnostic& last_diagnostic();
// Counts per event id (diagnostics/tests). Index = HSPEVENT_* value.
const int* event_counts(int* size);

void uninstall();

}  // namespace elona::platform::hsp
