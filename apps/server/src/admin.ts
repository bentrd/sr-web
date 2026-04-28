import type { Server } from "bun";
import type { WsData } from "./rooms";

type SrServer = Server<WsData>;
import {
	adminListScores,
	adminListRgScores,
	adminListTimeScores,
	adminUpdateScore,
	adminUpdateRgScore,
	adminUpdateTimeScore,
	adminDeleteScore,
	adminDeleteRgScore,
	adminDeleteTimeScore,
	adminInsertScore,
	adminInsertRgScore,
	adminInsertTimeScore,
	getAllTimeLeaderboard,
	getRgAllTimeLeaderboard,
	getTimeAllTimeLeaderboard,
} from "./leaderboard";
import type {
	Leaderboard,
	RgLeaderboard,
	TimeLeaderboard,
} from "@sr-web/protocol";

type Table = "scores" | "rg_scores" | "time_scores";

function json(body: unknown, status = 200): Response {
	return new Response(JSON.stringify(body), {
		status,
		headers: { "content-type": "application/json" },
	});
}

function todayDate(): string {
	return new Date().toISOString().slice(0, 10);
}

// Re-broadcast the public leaderboards so connected clients see admin
// edits immediately. Without this the live UI shows stale data until
// the next genuine submission triggers a republish.
function rebroadcast(server: SrServer, table: Table): void {
	if (table === "scores") {
		const lb = getAllTimeLeaderboard(10);
		server.publish(
			"leaderboard-speed",
			JSON.stringify({
				type: "leaderboard",
				date: todayDate(),
				entries: lb.map((e) => ({ rank: e.rank, name: e.name, maxSpeed: e.maxSpeed })),
			} satisfies Leaderboard),
		);
	} else if (table === "rg_scores") {
		const lb = getRgAllTimeLeaderboard(10);
		server.publish(
			"leaderboard-rg",
			JSON.stringify({
				type: "rg_leaderboard",
				date: todayDate(),
				entries: lb.map((e) => ({ rank: e.rank, name: e.name, maxStreak: e.maxStreak })),
			} satisfies RgLeaderboard),
		);
	} else {
		const lb = getTimeAllTimeLeaderboard(10);
		server.publish(
			"leaderboard-time",
			JSON.stringify({
				type: "time_leaderboard",
				date: todayDate(),
				entries: lb.map((e) => ({
					rank: e.rank,
					name: e.name,
					durationTicks: e.durationTicks,
					runId: e.runId,
				})),
			} satisfies TimeLeaderboard),
		);
	}
}

export async function handleAdminRequest(
	req: Request,
	url: URL,
	server: SrServer,
): Promise<Response | null> {
	if (!url.pathname.startsWith("/admin")) return null;

	const expected = process.env.ADMIN_TOKEN;
	if (!expected) {
		return new Response(
			"admin disabled — set ADMIN_TOKEN env var (e.g. `fly secrets set ADMIN_TOKEN=...`)",
			{ status: 503 },
		);
	}

	const provided = req.headers.get("x-admin-token") ?? url.searchParams.get("token");
	if (provided !== expected) {
		return new Response("unauthorized", { status: 401 });
	}

	if (url.pathname === "/admin" || url.pathname === "/admin/") {
		return new Response(ADMIN_HTML, {
			headers: { "content-type": "text/html; charset=utf-8" },
		});
	}

	const m = url.pathname.match(/^\/admin\/api\/(scores|rg_scores|time_scores)(?:\/(\d+))?$/);
	if (!m) return new Response("not found", { status: 404 });

	const table = m[1] as Table;
	const id = m[2] ? Number(m[2]) : null;

	function listFor(t: Table): unknown {
		if (t === "scores") return adminListScores();
		if (t === "rg_scores") return adminListRgScores();
		return adminListTimeScores();
	}
	function updateFor(t: Table, rowId: number, body: Record<string, unknown>): boolean {
		if (t === "scores") return adminUpdateScore(rowId, body);
		if (t === "rg_scores") return adminUpdateRgScore(rowId, body);
		return adminUpdateTimeScore(rowId, body);
	}
	function deleteFor(t: Table, rowId: number): boolean {
		if (t === "scores") return adminDeleteScore(rowId);
		if (t === "rg_scores") return adminDeleteRgScore(rowId);
		return adminDeleteTimeScore(rowId);
	}

	try {
		if (req.method === "GET" && id === null) {
			return json(listFor(table));
		}

		if (req.method === "POST" && id === null) {
			const body = (await req.json()) as Record<string, unknown>;
			const date = String(body.date ?? todayDate()).slice(0, 32);
			const name = String(body.player_name ?? "").slice(0, 24);
			const ts = Number(body.timestamp ?? Date.now());
			if (!name) return json({ error: "player_name required" }, 400);
			if (table === "scores") {
				const speed = Number(body.max_speed);
				if (!Number.isFinite(speed)) return json({ error: "max_speed required" }, 400);
				const newId = adminInsertScore(date, name, speed, ts);
				rebroadcast(server, table);
				return json({ id: newId });
			} else if (table === "rg_scores") {
				const streak = Number(body.max_streak);
				if (!Number.isFinite(streak)) return json({ error: "max_streak required" }, 400);
				const newId = adminInsertRgScore(date, name, streak, ts);
				rebroadcast(server, table);
				return json({ id: newId });
			} else {
				const ticks = Number(body.duration_ticks);
				if (!Number.isFinite(ticks)) return json({ error: "duration_ticks required" }, 400);
				const newId = adminInsertTimeScore(date, name, ticks, ts);
				rebroadcast(server, table);
				return json({ id: newId });
			}
		}

		if (req.method === "PATCH" && id !== null) {
			const body = (await req.json()) as Record<string, unknown>;
			const ok = updateFor(table, id, body);
			if (!ok) return json({ error: "no rows updated" }, 404);
			rebroadcast(server, table);
			return json({ ok: true });
		}

		if (req.method === "DELETE" && id !== null) {
			const ok = deleteFor(table, id);
			if (!ok) return json({ error: "not found" }, 404);
			rebroadcast(server, table);
			return json({ ok: true });
		}
	} catch (err) {
		return json({ error: err instanceof Error ? err.message : "unknown" }, 500);
	}

	return new Response("method not allowed", { status: 405 });
}

const ADMIN_HTML = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8" />
<title>SR-web · leaderboard admin</title>
<meta name="viewport" content="width=device-width, initial-scale=1" />
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 24px;
    background: #0e0e12; color: #e6e6ea;
    font: 14px/1.4 ui-monospace, "SF Mono", Menlo, monospace;
  }
  h1 { font-size: 16px; margin: 0 0 16px; font-weight: 600; }
  .tabs { display: flex; gap: 8px; margin-bottom: 16px; }
  .tabs button {
    background: #1c1c22; color: #aaa; border: 1px solid #2a2a32;
    padding: 6px 14px; border-radius: 6px; cursor: pointer; font: inherit;
  }
  .tabs button.active { background: #2d4a8a; color: #fff; border-color: #3a5fb0; }
  .toolbar {
    display: flex; gap: 8px; margin-bottom: 12px;
    align-items: center; flex-wrap: wrap;
  }
  .toolbar input[type="search"] {
    background: #1c1c22; border: 1px solid #2a2a32; color: #e6e6ea;
    padding: 4px 10px; border-radius: 4px; font: inherit; min-width: 220px;
  }
  button.action {
    background: #1c1c22; color: #e6e6ea; border: 1px solid #2a2a32;
    padding: 4px 10px; border-radius: 4px; cursor: pointer; font: inherit;
  }
  button.action:hover:not(:disabled) { background: #25252d; }
  button.action:disabled { opacity: 0.4; cursor: not-allowed; }
  button.danger { color: #ff7676; border-color: #5a2828; }
  button.danger:hover:not(:disabled) { background: #2a1818; }
  button.primary { background: #2d4a8a; border-color: #3a5fb0; color: #fff; }
  button.primary:hover:not(:disabled) { background: #355aa8; }
  table { width: 100%; border-collapse: collapse; font-size: 13px; }
  th, td {
    padding: 6px 10px; text-align: left;
    border-bottom: 1px solid #1f1f26; vertical-align: middle;
  }
  th { color: #888; font-weight: 500; background: #15151b; position: sticky; top: 0; z-index: 1; }
  th.sortable { cursor: pointer; user-select: none; }
  th.sortable:hover { color: #e6e6ea; }
  th .sort-arrow { color: #5fa8ff; margin-left: 4px; }
  td input[type="text"] {
    width: 100%; background: transparent; border: 1px solid transparent;
    color: inherit; font: inherit; padding: 2px 4px; border-radius: 3px;
  }
  td input[type="text"]:focus { outline: none; border-color: #3a5fb0; background: #15151b; }
  tr.dirty { background: #1c1a14; }
  tr.dirty td input[type="text"] { border-color: #5a4a1f; }
  tr.selected { background: #16223a; }
  tr.dirty.selected { background: #1c2030; }
  .row-actions { white-space: nowrap; }
  .empty { color: #666; padding: 40px; text-align: center; }
  .ts { color: #888; font-size: 12px; }
  .col-check { width: 32px; text-align: center; }
  .col-check input { cursor: pointer; }
  #summary { color: #888; font-size: 12px; }
  #status { margin-left: auto; color: #888; font-size: 12px; }
  #status.error { color: #ff7676; }
  #status.ok { color: #6fdc8c; }
</style>
</head>
<body>
<h1>SR-web · leaderboard admin</h1>

<div class="tabs">
  <button id="tab-scores" class="active">Speed scores</button>
  <button id="tab-rg">RG streaks</button>
  <button id="tab-time">Time runs</button>
</div>

<div class="toolbar">
  <input type="search" id="search" placeholder="Filter by player name…" autocomplete="off" />
  <button class="action primary" id="add">+ Add row</button>
  <button class="action" id="refresh">Refresh</button>
  <button class="action" id="bulk-save" disabled>Save selected</button>
  <button class="action danger" id="bulk-delete" disabled>Delete selected</button>
  <span id="summary"></span>
  <span id="status"></span>
</div>

<table id="grid">
  <thead></thead>
  <tbody></tbody>
</table>

<script>
(() => {
  const params = new URLSearchParams(location.search);
  const token = params.get('token');
  if (!token) {
    document.body.innerHTML = '<h1>missing ?token=...</h1>';
    return;
  }

  let table = 'scores';
  const cols = {
    scores: ['id', 'date', 'player_name', 'max_speed', 'timestamp'],
    rg_scores: ['id', 'date', 'player_name', 'max_streak', 'timestamp'],
    time_scores: ['id', 'date', 'player_name', 'duration_ticks', 'timestamp'],
  };
  const editableCols = {
    scores: ['date', 'player_name', 'max_speed', 'timestamp'],
    rg_scores: ['date', 'player_name', 'max_streak', 'timestamp'],
    time_scores: ['date', 'player_name', 'duration_ticks', 'timestamp'],
  };
  const numericCols = new Set(['id', 'max_speed', 'max_streak', 'duration_ticks', 'timestamp']);

  // per-tab state, reset on tab switch / reload
  let rows = [];
  let sort = { col: 'id', dir: 'desc' };
  let filter = '';
  const selected = new Set();
  const dirty = new Set();

  const statusEl = document.getElementById('status');
  const summaryEl = document.getElementById('summary');
  const searchEl = document.getElementById('search');
  const bulkDeleteEl = document.getElementById('bulk-delete');
  const bulkSaveEl = document.getElementById('bulk-save');
  const grid = document.getElementById('grid');

  function setStatus(msg, kind) {
    statusEl.textContent = msg || '';
    statusEl.className = kind || '';
    if (kind === 'ok') setTimeout(() => { if (statusEl.textContent === msg) statusEl.textContent = ''; }, 2000);
  }

  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }

  async function api(path, opts = {}) {
    const r = await fetch('/admin/api' + path, {
      ...opts,
      headers: { 'content-type': 'application/json', 'x-admin-token': token, ...(opts.headers || {}) },
    });
    if (!r.ok) {
      const t = await r.text();
      throw new Error(r.status + ': ' + t);
    }
    return r.json();
  }

  function fmtTs(ms) {
    if (!ms) return '';
    const d = new Date(Number(ms));
    if (Number.isNaN(d.getTime())) return '';
    return d.toISOString().replace('T', ' ').slice(0, 19);
  }

  function compareRows(a, b, col) {
    const va = a[col], vb = b[col];
    if (numericCols.has(col)) {
      const na = Number(va) || 0, nb = Number(vb) || 0;
      return na - nb;
    }
    return String(va ?? '').localeCompare(String(vb ?? ''));
  }

  function visibleRows() {
    const f = filter.trim().toLowerCase();
    let arr = rows.slice();
    if (f) arr = arr.filter(r => String(r.player_name ?? '').toLowerCase().includes(f));
    arr.sort((a, b) => {
      const c = compareRows(a, b, sort.col);
      return sort.dir === 'asc' ? c : -c;
    });
    return arr;
  }

  function updateSummary() {
    const view = visibleRows();
    const parts = [view.length + (rows.length !== view.length ? ' / ' + rows.length : '') + ' rows'];
    if (dirty.size) parts.push(dirty.size + ' edited');
    if (selected.size) parts.push(selected.size + ' selected');
    summaryEl.textContent = parts.join(' · ');
  }

  function updateBulkUi() {
    const count = selected.size;
    bulkDeleteEl.textContent = 'Delete selected' + (count ? ' (' + count + ')' : '');
    bulkSaveEl.textContent = 'Save selected' + (count ? ' (' + count + ')' : '');
    bulkDeleteEl.disabled = count === 0;
    let dirtySelected = 0;
    selected.forEach(id => { if (dirty.has(id)) dirtySelected++; });
    bulkSaveEl.disabled = dirtySelected === 0;
  }

  async function load() {
    setStatus('loading…');
    try {
      rows = await api('/' + table);
      dirty.clear();
      selected.clear();
      render();
      setStatus(rows.length + ' rows', 'ok');
    } catch (e) {
      setStatus(e.message, 'error');
    }
  }

  function render() {
    const thead = grid.querySelector('thead');
    const tbody = grid.querySelector('tbody');
    const headers = cols[table];
    const view = visibleRows();
    const allSelectedVisible = view.length > 0 && view.every(r => selected.has(r.id));

    const arrow = (col) => sort.col === col
      ? '<span class="sort-arrow">' + (sort.dir === 'asc' ? '▲' : '▼') + '</span>'
      : '';
    thead.innerHTML =
      '<tr>' +
        '<th class="col-check"><input type="checkbox" id="check-all"' + (allSelectedVisible ? ' checked' : '') + ' /></th>' +
        headers.map(c => '<th class="sortable" data-sort="' + c + '">' + c + arrow(c) + '</th>').join('') +
        '<th></th>' +
      '</tr>';

    if (view.length === 0) {
      tbody.innerHTML = '<tr><td colspan="' + (headers.length + 2) + '" class="empty">no rows</td></tr>';
      updateSummary();
      updateBulkUi();
      return;
    }

    tbody.innerHTML = view.map(row => {
      const isSel = selected.has(row.id);
      const isDirty = dirty.has(row.id);
      const cells = headers.map(c => {
        if (c === 'id') return '<td>' + row.id + '</td>';
        if (c === 'timestamp') {
          return '<td><input type="text" data-field="timestamp" data-id="' + row.id + '" value="' + escapeHtml(row.timestamp ?? '') + '" /><div class="ts">' + escapeHtml(fmtTs(row.timestamp)) + '</div></td>';
        }
        const v = row[c] ?? '';
        return '<td><input type="text" data-field="' + c + '" data-id="' + row.id + '" value="' + escapeHtml(v) + '" /></td>';
      }).join('');
      const cls = [isDirty ? 'dirty' : '', isSel ? 'selected' : ''].filter(Boolean).join(' ');
      return '<tr class="' + cls + '" data-id="' + row.id + '">' +
        '<td class="col-check"><input type="checkbox" data-check="' + row.id + '"' + (isSel ? ' checked' : '') + ' /></td>' +
        cells +
        '<td class="row-actions">' +
          '<button class="action" data-save="' + row.id + '">Save</button> ' +
          '<button class="action danger" data-del="' + row.id + '">Delete</button>' +
        '</td></tr>';
    }).join('');

    updateSummary();
    updateBulkUi();
  }

  function collectRow(id) {
    const inputs = grid.querySelectorAll('input[data-id="' + id + '"]');
    const out = {};
    const fields = editableCols[table];
    inputs.forEach(i => {
      const field = i.dataset.field;
      if (!fields.includes(field)) return;
      let v = i.value;
      if (numericCols.has(field)) v = Number(v);
      out[field] = v;
    });
    return out;
  }

  async function saveRow(id) {
    const body = collectRow(id);
    delete body.id;
    await api('/' + table + '/' + id, { method: 'PATCH', body: JSON.stringify(body) });
    dirty.delete(id);
    const idx = rows.findIndex(r => r.id === id);
    if (idx >= 0) rows[idx] = { ...rows[idx], ...body };
  }

  async function deleteRow(id) {
    await api('/' + table + '/' + id, { method: 'DELETE' });
    rows = rows.filter(r => r.id !== id);
    dirty.delete(id);
    selected.delete(id);
  }

  // Click handler: row Save/Delete buttons + sortable header
  grid.addEventListener('click', async (e) => {
    const t = e.target;
    if (!(t instanceof HTMLElement)) return;

    const saveId = t.dataset.save;
    if (saveId) {
      try { await saveRow(Number(saveId)); setStatus('saved #' + saveId, 'ok'); render(); }
      catch (err) { setStatus(err.message, 'error'); }
      return;
    }

    const delId = t.dataset.del;
    if (delId) {
      if (!confirm('Delete row ' + delId + '?')) return;
      try { await deleteRow(Number(delId)); setStatus('deleted #' + delId, 'ok'); render(); }
      catch (err) { setStatus(err.message, 'error'); }
      return;
    }

    const th = t.closest('th.sortable');
    if (th) {
      const col = th.dataset.sort;
      if (!col) return;
      if (sort.col === col) sort.dir = sort.dir === 'asc' ? 'desc' : 'asc';
      else { sort.col = col; sort.dir = numericCols.has(col) ? 'desc' : 'asc'; }
      render();
    }
  });

  // Change handler: master + per-row checkboxes
  grid.addEventListener('change', (e) => {
    const t = e.target;
    if (!(t instanceof HTMLInputElement)) return;
    if (t.id === 'check-all') {
      const view = visibleRows();
      if (t.checked) view.forEach(r => selected.add(r.id));
      else view.forEach(r => selected.delete(r.id));
      render();
      return;
    }
    const checkId = t.dataset.check;
    if (checkId) {
      const id = Number(checkId);
      if (t.checked) selected.add(id); else selected.delete(id);
      const tr = grid.querySelector('tr[data-id="' + id + '"]');
      if (tr) tr.classList.toggle('selected', t.checked);
      // sync master checkbox without full re-render so input focus is preserved
      const view = visibleRows();
      const master = grid.querySelector('#check-all');
      if (master) master.checked = view.length > 0 && view.every(r => selected.has(r.id));
      updateSummary();
      updateBulkUi();
    }
  });

  // Input handler: dirty tracking (no re-render — would steal focus while typing)
  grid.addEventListener('input', (e) => {
    const t = e.target;
    if (!(t instanceof HTMLInputElement)) return;
    const idStr = t.dataset.id;
    if (!idStr) return;
    const id = Number(idStr);
    if (!dirty.has(id)) {
      dirty.add(id);
      const tr = grid.querySelector('tr[data-id="' + id + '"]');
      if (tr) tr.classList.add('dirty');
      updateSummary();
      updateBulkUi();
    }
  });

  searchEl.addEventListener('input', () => {
    filter = searchEl.value;
    render();
  });

  document.getElementById('refresh').addEventListener('click', load);

  document.getElementById('add').addEventListener('click', async () => {
    const name = prompt('Player name?');
    if (!name) return;
    const valueLabel =
      table === 'scores' ? 'max_speed'
      : table === 'rg_scores' ? 'max_streak'
      : 'duration_ticks';
    const valStr = prompt(valueLabel + '?');
    if (valStr === null) return;
    const val = Number(valStr);
    if (!Number.isFinite(val)) { setStatus('invalid number', 'error'); return; }
    const body = { player_name: name, date: new Date().toISOString().slice(0, 10), timestamp: Date.now() };
    body[valueLabel] = val;
    try {
      await api('/' + table, { method: 'POST', body: JSON.stringify(body) });
      setStatus('inserted', 'ok');
      await load();
    } catch (err) { setStatus(err.message, 'error'); }
  });

  bulkDeleteEl.addEventListener('click', async () => {
    if (selected.size === 0) return;
    if (!confirm('Delete ' + selected.size + ' selected row(s)? This cannot be undone.')) return;
    const ids = [...selected];
    setStatus('deleting ' + ids.length + '…');
    let ok = 0, fail = 0;
    for (const id of ids) {
      try { await deleteRow(id); ok++; } catch (e) { fail++; }
    }
    render();
    setStatus('deleted ' + ok + (fail ? ', ' + fail + ' failed' : ''), fail ? 'error' : 'ok');
  });

  bulkSaveEl.addEventListener('click', async () => {
    const ids = [...selected].filter(id => dirty.has(id));
    if (ids.length === 0) return;
    setStatus('saving ' + ids.length + '…');
    let ok = 0, fail = 0;
    for (const id of ids) {
      try { await saveRow(id); ok++; } catch (e) { fail++; }
    }
    render();
    setStatus('saved ' + ok + (fail ? ', ' + fail + ' failed' : ''), fail ? 'error' : 'ok');
  });

  const tabIds = ['tab-scores', 'tab-rg', 'tab-time'];
  function activate(id, t) {
    table = t;
    sort = { col: 'id', dir: 'desc' };
    filter = '';
    searchEl.value = '';
    selected.clear();
    dirty.clear();
    for (const tid of tabIds) {
      document.getElementById(tid).classList.toggle('active', tid === id);
    }
    load();
  }
  document.getElementById('tab-scores').addEventListener('click', () => activate('tab-scores', 'scores'));
  document.getElementById('tab-rg').addEventListener('click', () => activate('tab-rg', 'rg_scores'));
  document.getElementById('tab-time').addEventListener('click', () => activate('tab-time', 'time_scores'));

  load();
})();
</script>
</body>
</html>`;
