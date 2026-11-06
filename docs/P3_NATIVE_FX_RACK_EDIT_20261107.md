# Native FX editor lifetime and rack editing

Continues the owner-tested working candidate `2b1ef483` in #418. The owner
explicitly requested these fixes, visual removal of MAIN cable fragments,
reconciliation and merge to main. Physical routing/FX processing was reported
working; full ASIO/device/long-session qualification remains separate.

## Editor flash root causes

The 12 Hz retention check searched inactive M4 `fxInstances` even when native
ChannelId owned the plugin. It therefore closed the newly opened native
editor. Retention now queries the current processor lease by pointer and device
epoch. It survives reorder and channel transfer, and closes on actual removal
or device invalidation. Generic device-manager notifications no longer close
an otherwise valid editor. Reopening also destroys the old window before
asking JUCE for an editor pointer, avoiding a dangling returned editor.

## One native FX edit transaction

REMOVE_INSERT and MOVE_INSERT require current device/PCM/roster revisions,
exact source ChannelId, slot and opaque instance token. Destination ChannelId,
role and insertion position are validated independently against public/native
inventories. Both roster catalog order and actual processor lease order prepare
off callback and publish once. Cross-channel MOVE transfers the existing
processor and saved bypass/state; it does not rescan, clone or prepare a VST.
PDC is rebuilt from the new placement; no new callback or JS DSP exists.

ACK alone does not move/delete GUI rows. Later accepted readback must prove
the exact new ordered token inventories and global absence/uniqueness of the
removed/moved instance. Stale tokens, full racks, invalid positions and an
occupied external CH1 owner reject without replacing the audible graph.

## Shared approved GUI

The historical HTML seed is preserved. The sole Pages assembler adds shared
FX interaction semantics, used identically by public `/preview/` and native
WebView2. A row-sized ghost follows pointer motion with an insertion marker;
dragging up/down or into another visible channel rack dispatches one native
edit. Escape, invalid release and pointer cancellation discard presentation.
Keyboard Alt+arrow reorder uses the same native authority. Public reference
transfers retain identity and compact slots, without claiming audio.

MAIN's decorative source-down/master-down cable fragments are no longer
rendered. Actual MAIN edges, jack identity, OUT controls, disconnect/Undo and
custom channel-to-channel cables remain available.

## Verification and merge

- Native JUCE tests: noncommutative gain/offset PCM verifies initial order,
  reorder, cross-channel transfer and delete; exact owner/editor retention,
  no processor re-prepare, stale rejection and zero render allocations.
- Transport tests: exact ordered ACK/readback, no token copy masquerading as
  transfer, deletion absence, stale instance rejection and editor guard source.
- Browser tests: compiled native pointer ghost movement, insertion marker,
  reorder/transfer/delete, editor token after transfer, and quiet MAIN while
  the audio edge remains, at three desktop sizes. Backend isolation is emulated.
- Existing approved-GUI provenance and current browser/PCM suites remain required.

Reconcile main's one documentation-only commit `ac85039a` as a parent of the
candidate before exact-SHA qualification. Merge only the qualified source under
the owner's explicit instruction; do not label main merge as a stable release
or invent completion of physical FAT for the newly added interactions.
