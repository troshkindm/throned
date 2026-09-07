---
name: ui-notices
description: Add or change Throned's in-app tips, dismissible warnings, subscription announcements or shared updater footer. Use when deciding message placement, eligibility, priority, actions or dismissal persistence.
---

# In-app notices

Choose the surface from the message's scope. Control-specific help belongs in
the existing tooltip or field description. Subscription announcements belong to
their group. App-wide tips and dismissible warnings use the shared footer.
Keep errors needed to complete a current form or operation at that operation;
an optional footer message must not become its only feedback.

The footer is `UpdateStatusWidget`, despite the update-specific class name.
`PostPassiveWarning()` is a separate non-modal message box, not this queue;
changing an existing caller's delivery is a separate behavior decision.

## Extend the shared footer

Read [Footer notices](../../../docs/development.md#footer-notices) for the queue
contract, then follow `src/ui/widget/WindowNotices.cpp` for a feature tip.
Keep eligibility, action and persistence in the producer; the widget presents
notices and arbitrates the slot. Add a feature-specific producer when the policy
does not belong with the existing one.

- Give the notice a stable ID. Reposting that ID updates it. Do not rotate IDs
  on every release or wording change: that would undo the user's dismissal.
- Define when it becomes eligible and when it stops being useful. Recheck on
  relevant state changes and remove obsolete notices, including when the user
  completes the suggested action elsewhere. Mica uses the available skin
  catalogue as its platform check.
- Decide what dismissal means. `dismissed_notices` persists handled feature
  tips; a recurring fault may need a different lifetime. Never remember a tip
  as dismissed merely because an update temporarily occupies its slot.
- Preserve update progress, restart and retry actions. An update owns the slot
  until dismissed; queued notices resume afterward. An informational message
  must not use the update-error state to get attention.
- Select severity from the application's knowledge of the condition. Provider
  announcement text alone does not establish warning or error severity.
- Keep titles and details plain text, actions short, and strings translated
  under the repository's translation rules. Optional actions need to leave the
  underlying setting accessible in its usual place.

## Verify the behavior you change

Extend `tests/test_window_notices.cpp` for changed queue or producer behavior.
Relevant cases include competing updates, replacement by ID, action routing,
eligibility becoming false, and dismissal across a settings reload. Persistence
tests use a temporary database. UI calls belong on the GUI thread.

Run the normal build and tests for application changes. For footer layout,
`main-shell` in `script/run_ui_scenarios.cmake` covers all updater states; use
`subscription` for the provider announcement. Inspect the changed message with
the normal and affected material theme, including its longest translated action.

Ordinary previews suppress feature tips. For interactive Mica-tip inspection,
use the flags in [Footer notices](../../../docs/development.md#footer-notices):
an isolated non-Mica preview offers the tip, a synthetic ready update can take
precedence, and Later reveals the queued tip. Restart and retry are inert in
preview mode. Keep preview eligibility opt-in and never use a real profile to
exercise a tip that changes settings.
