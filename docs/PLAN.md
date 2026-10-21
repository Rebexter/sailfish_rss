# SailRSS development plan

SailRSS is a native Sailfish OS client for [FreshRSS](https://freshrss.org/),
aimed at eventual publication in the Jolla Store (Harbour).

## Decisions

- **Language:** C++17 core with a QML/Silica UI. The target ships Qt 5.6.3,
  so no Qt 5.7+ APIs.
- **API:** FreshRSS's Google Reader compatible API (`/api/greader.php`).
  The Fever API is not used: it cannot manage subscriptions and has weaker auth.
- **Offline first:** the UI reads only from a local SQLite database. Read and
  star changes are stored locally, queued, and pushed on the next sync.
- **Store:** Jolla Store (Harbour). Every build must pass
  `scripts/dev.sh check`: only allowed libraries, QML imports and install
  paths, no background daemons. Sync runs only while the app is open.
- **Sandbox:** Sailjail organization `io.github.rebexter`, application
  `harbour-sailrss`. App data lives in
  `~/.local/share/io.github.rebexter/harbour-sailrss`.

## Architecture

```
┌───────────────────── QML / Silica UI ──────────────────────┐
│ LoginPage   CategoriesPage   ArticleListPage   ArticlePage │
│ SettingsPage                                    CoverPage  │
└───────────────▲────────────────────────────────────────────┘
                │  list models + Q_INVOKABLE calls
┌───────────────┴──────────────── C++ core ──────────────────┐
│  FeedModel   ArticleModel          SyncManager             │
│      │            │          (pull new / push pending)     │
│      └──── Database (SQLite) ◄──────┤                      │
│            feeds, tags, items,      │                      │
│            pending_actions     GReaderClient (HTTP, JSON)  │
└─────────────────────────────────────┬──────────────────────┘
                                      │ HTTPS
                         FreshRSS  /api/greader.php
```

## Phases

Each phase ends with something that can be checked on the phone.

### 0. Scaffold ✅
- CMake project from the SDK template, named `harbour-sailrss`.
- `.gitignore`, RPM spec, `scripts/dev.sh` (build, check, deploy).
- Done when: the empty app builds, passes the Harbour validator, deploys and
  launches.

### 1. API client (C++, no UI) ✅
- Log in with the FreshRSS API password and get an auth token.
- Fetch subscriptions, tags and unread counts.
- Fetch articles in pages, using the continuation token and a "since"
  timestamp.
- Mark items read or starred, mark all as read.
- Unit tests against saved JSON responses; manual tests against a local
  FreshRSS in Podman (`freshrss/freshrss`) and the real server.
- Test credentials come from the environment variables `SAILRSS_URL`,
  `SAILRSS_USER` and `SAILRSS_PASSWORD`. **This is for development only**:
  remove it in Phase 4, when real credential storage replaces it.
- Done when: tests pass against the local FreshRSS.

### 2. Storage and sync ✅
- SQLite tables: feeds (with their category), items, pending actions, sync
  state.
- User actions (read, starred, mark all as read) change the database and
  queue a pending action in one transaction. One action per item and field;
  the newest replaces the older, and actions are sent in order.
- Sync: send pending actions, replace the feed list, fetch articles that
  arrived since the last sync (FreshRSS's `ot` filter goes by arrival time,
  so late-published articles are included), then set read and starred state
  from the server's complete unread and starred ID lists. The `ot` filter
  does not report state changes, so this reconciliation is needed.
- History window (default 600 days): the first sync fetches that far back;
  starred articles are always fetched, however old. Read, unstarred
  articles older than the window are deleted.
- Not stored yet: user labels and enclosures (podcasts, images).
- Done when: a sync fills the database and toggling read state survives being
  offline.

### 3. Core UI ✅
- Login page with a connection check; the server address is accepted as
  users know it.
- Folder list with "All articles" and "Starred", feeds per folder, unread
  counts.
- Article list (unread only or all) with pull-to-refresh, mark all as read,
  and read/star from the context menu.
- Article page: marked read on open, star, mark unread, open in browser.
- Article rendering: Silica rich-text label. Images, scripts and embedded
  content are removed for now; `Sailfish.WebView` (full HTML and images) is
  still an option.
- User actions are sent a few seconds after they happen (send-only sync);
  a full sync runs on start, on refresh and from the cover.
- **Temporary:** the session token (never the password) is stored in plain
  text in the app's sandboxed `settings.ini` until Phase 4.
- Done when: reading feeds works day to day.

### 4. Sailfish integration
- Cover with unread count.
- Background refresh while the app is open (`Nemo.KeepAlive`).
- New-article notifications (`Nemo.Notifications`).
- Proper credential management: the login page stores the server URL and
  user, and the API password goes in the system keychain (`Sailfish.Secrets`,
  adds the `Secrets` permission). Move the session token out of
  `settings.ini` into the keychain too, and remove the `SAILRSS_*`
  environment variable test path from Phase 1 at the same time.
- Share (`Sailfish.Share`), settings page, translations.

### 5. Release
- Stripped release build, real icon, store description and screenshots.
- Check that no `SAILRSS_*` environment variable handling is left in the app,
  and that no token is stored in `settings.ini`.
- Harbour validator clean, then submit to the Jolla Store.

### Later
- Add and remove subscriptions in the app.
- Search.
- Reader mode that fetches full article text.
