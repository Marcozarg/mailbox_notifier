---
description: Bump version, update header/CHANGELOG/CLAUDE.md/README/knowledge base, commit and push a mailbox notifier release
argument-hint: <receiver|sender> <patch|minor|major> "<description>" | node-red <flow> <patch|minor|major> "<description>"
allowed-tools: Read, Edit, Glob, Grep, Bash(git *)
---

You are executing a release for the mailbox notifier project. Parse `$ARGUMENTS` and follow the steps below exactly.

## Versioning rules (from CLAUDE.md — enforce these)

| Bump | When to use |
|---|---|
| `patch` | Bug fix, comment, typo, cosmetic only |
| `minor` | Feature, behaviour tweak, UX change |
| `major` | Hardware swap, MCU change, packet format break |

If the requested bump type seems inconsistent with the description, say so before proceeding.

---

## Step 1 — Parse arguments

Extract from `$ARGUMENTS`:
- **component**: `receiver` | `sender` | `node-red`
- **flow** (node-red only): `mail_arrived` | `battery_low` | `sender_boot` | `no_mail_alert`
- **bump**: `patch` | `minor` | `major` (for node-red, default to `minor` if omitted)
- **description**: the rest of the string (strip surrounding quotes)

If component is missing or unrecognised, stop and ask.

---

## Step 2a — Firmware release (component = `receiver` or `sender`)

### Target file
- `receiver` → `firmware/mailbox_receiver/mailbox_receiver.ino`
- `sender`   → `firmware/mailbox_sender/mailbox_sender.ino`

### Actions

1. **Read the `.ino` file.** Find the line matching `#define FW_VERSION "Vx.y.z"` and extract the current version.

2. **Compute new version** by applying the bump type to the current version (e.g. V2.1.1 + patch → V2.1.2; V2.1.1 + minor → V2.2.0; V2.1.1 + major → V3.0.0).

3. **Update `#define FW_VERSION`** — replace the old version string with the new one. This is the single source of truth; do not hardcode the version anywhere else.

4. **Insert a new version header block** at the very top of the file, above all existing header blocks, using this exact format (em-dashes, bullet •):
   ```
   // Vx.y.z — YYYY-MM-DD — <description>
   //
   // Vx.y.z changes:
   //   • <description>
   //
   ```
   Use today's date. Do not modify the existing header blocks below it.

5. **Update `CHANGELOG.md`** — find the correct section (`## Sender` or `## Receiver`) and prepend a new entry immediately after the section heading:
   ```
   ### Vx.y.z — YYYY-MM-DD
   - <description>
   ```

6. **Update `CLAUDE.md`** — find the version table row for this component and update the version string. CLAUDE.md is tracked; stage it with the release.

7. **Update `README.md`** — read the file and check every section that could be affected by this change:
   - `## MQTT topic tree` table: if any `publishOne(` calls changed their `retained` argument, update the `Retained` column for the affected topics.
   - `## Home Assistant integration`: if `publishOneDiscovery(` calls were added/removed, update the entity count and entity list table.
   - `## Packet format` / key=value field table: if new fields were added or removed from the packet, update the table.
   - Any prose section that describes behaviour that has changed.
   Only edit README.md if something actually needs updating. If nothing changed that README covers, skip it.

8. **Update knowledge base** — edit `O:\GITHUB\KNOWLEDGE\projects.md` and update the version line for this component under `### mailbox_notifier`. Do not commit this file to the mailbox_notifier repo.

9. **Sender-specific warning** — if component is `sender`, output this message prominently:
   > Sender firmware changed. Flashing requires a physical trip to the mailbox with a USB cable. The receiver can be updated via OTA; the sender cannot.

10. **Commit and push:**
    ```
    git add <ino-file> CHANGELOG.md CLAUDE.md [README.md if changed]
    git commit -m "<Component> Vx.y.z: <description>"
    git push
    ```
    Never stage `arduino_secrets.h` or anything in `HA config/`.
    Then commit and push the knowledge base separately:
    ```
    git -C O:\GITHUB\KNOWLEDGE add projects.md
    git -C O:\GITHUB\KNOWLEDGE commit -m "Update mailbox <component> to Vx.y.z"
    git -C O:\GITHUB\KNOWLEDGE push
    ```

---

## Step 2b — Node-RED release (component = `node-red`)

### Target file
Each flow carries its own version. Arguments for this component: `node-red <flow> <patch|minor|major> "<description>"`,
where `<flow>` is one of:
- `mail_arrived` → `Node-RED/Node-RED_mail_arrived.txt`
- `battery_low` → `Node-RED/Node-RED_battery_low.txt`
- `sender_boot` → `Node-RED/Node-RED_sender_boot.txt`
- `no_mail_alert` → `Node-RED/Node-RED_no_mail_alert.txt`

If the flow is missing or unrecognised, stop and ask. When one change touches several flows, release each flow separately.

### Actions

1. **Read the flow file.** Find the comment node (type `"comment"`) and extract the current version from its `name` field (e.g. `"Mail Arrived — V2.0.0 — 2026-06-03"`).

2. **Compute new version** by applying the bump type (default minor) to that flow's own version.

3. **Update the flow file** — find the comment node and update:
   - `name`: replace old version and date with new version and today's date, keeping the flow title prefix (e.g. `"Mail Arrived — V2.1.0 — 2026-06-03"`)
   - `info`: prepend a new version entry at the top of the existing `info` string:
     ```
     Vx.y.z — YYYY-MM-DD — <description>\n\nVx.y.z changes:\n  • <description>\n\n
     ```
     Keep the previous version history below.

4. **Update `CHANGELOG.md`** — find `## Node-RED flows` and prepend:
   ```
   ### Vx.y.z — YYYY-MM-DD
   - `Node-RED_<flow>.txt`: <description>
   ```

5. **Update `README.md`** — check the Node-RED flows table (`## Node-RED flows` section) and update any behaviour descriptions that changed.

6. **Update knowledge base** — edit `O:\GITHUB\KNOWLEDGE\projects.md` and update the Node-RED version if tracked. Update `O:\GITHUB\KNOWLEDGE\node-red-patterns.md` if the change introduces a new reusable pattern.

7. **Commit and push:**
   ```
   git add Node-RED/Node-RED_<flow>.txt CHANGELOG.md [README.md if changed]
   git commit -m "Node-RED <flow> Vx.y.z: <description>"
   git push
   ```
   Then commit and push the knowledge base separately.

---

## After completing

Report:
- Old version → new version
- Files changed (list each one)
- Commit hash (from git output)
- For sender: repeat the physical-flash warning
