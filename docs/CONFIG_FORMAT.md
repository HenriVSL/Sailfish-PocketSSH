# Export / import format

PocketSSH uses the same JSON format as [Pocket Shell](https://github.com/HenriVSL/Pocket-Shell)
for iOS, so an export from either app imports into the other.

- Export all hosts: pull down on the host list ▸ **Export all hosts**.
- Export one host: long-press the host ▸ **Export**.
- Import: pull down ▸ **Import hosts**, then pick the `.json` file.

Exports are written to `~/Documents` as `pocketssh-hosts.json` (all hosts) or
`pocketssh-<host name>.json` (one host), with `-with-secrets` added when passwords and keys
are included. An existing file is never overwritten: `-2`, `-3`, … is appended instead.

## Schema (version 1)

```json
{
  "format": "pocket-shell-config",
  "version": 1,
  "exportedAt": "2026-09-26T12:00:00Z",
  "includesSecrets": false,
  "hosts": [
    {
      "id": "8C1A2F0E-5B7D-4C39-9E1A-2D6F0B3C4A55",
      "name": "web server",
      "hostname": "example.com",
      "port": 22,
      "username": "deploy",
      "authMethod": "password"
    }
  ]
}
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `format` | string | yes | Must be `pocket-shell-config` |
| `version` | int | yes | Currently `1` |
| `exportedAt` | ISO-8601 string | yes | Informational |
| `includesSecrets` | bool | yes | Informational |
| `hosts[].id` | UUID string | no | Used to update an existing host on import; a new UUID is generated if missing |
| `hosts[].name` | string | yes | Display name (may be empty) |
| `hosts[].hostname` | string | yes | Hostname or IP address |
| `hosts[].port` | int | no | Defaults to 22 |
| `hosts[].username` | string | yes | |
| `hosts[].authMethod` | `"password"` or `"privateKey"` | no | Defaults to `privateKey` if a key is present, otherwise `password` |
| `hosts[].password` | string | no | Only present when exported with secrets |
| `hosts[].privateKey` | string | no | OpenSSH or PEM text. Only present when exported with secrets |
| `hosts[].passphrase` | string | no | Only present when exported with secrets |

## Secrets

If you choose **Include passwords and keys**, the secrets are written in **plain text**.
Treat that file like the credentials themselves. PocketSSH writes files with secrets readable
only by your user (mode 0600), but `~/Documents` is a shared folder that other apps with the
Documents permission can read. Move the file somewhere safe or delete it once you've imported
it on the other device.

## Import rules

- A host whose `id` matches an existing host replaces that host; any other host is added.
- If the file contains secrets, they are saved in PocketSSH's private secret store, field by
  field: a field the file doesn't have (for example no password) leaves the saved one as it is.
  A private key in the file replaces the saved key and its passphrase together.
- Hosts with an empty `hostname` or a port outside 1–65535 are skipped.
- Files with any other `format` value are rejected.

## Writing a file by hand

You can create a file to import in any text editor. For example, to add two hosts:

```json
{
  "format": "pocket-shell-config",
  "version": 1,
  "exportedAt": "2026-01-01T00:00:00Z",
  "includesSecrets": false,
  "hosts": [
    { "name": "pi", "hostname": "192.168.1.20", "username": "pi" },
    { "name": "vps", "hostname": "vps.example.com", "port": 2222, "username": "me", "authMethod": "privateKey" }
  ]
}
```
