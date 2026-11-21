'use strict';
const byId = id => document.getElementById(id);
const make = (tag, text) => Object.assign(document.createElement(tag), {textContent: text});
let reports = [];
function render() {
  const term = byId('query').value.trim().toLowerCase();
  const stage = byId('stage').value;
  const groups = new Map();
  for (const row of reports) {
    if (stage && row[3] !== stage) continue;
    if (!row.slice(0, 2).join(' ').toLowerCase().includes(term)) continue;
    if (!groups.has(row[0])) groups.set(row[0], []);
    groups.get(row[0]).push(row);
  }
  const games = byId('games');
  games.replaceChildren();
  byId('status').textContent = groups.size ? groups.size + ' 款游戏' : '没有符合条件的实测记录';
  for (const [id, rows] of groups) {
    const entry = make('article', '');
    entry.className = 'entry';
    const head = make('header', '');
    head.append(make('h2', rows[0][1]));
    entry.append(head, make('small', 'Steam AppID ' + id));
    const list = make('ul', '');
    for (const [, ,version, result, detail, setup, source] of rows) {
      const row = make('li', '');
      row.append(make('strong', result), document.createTextNode(' · ' + detail));
      const meta = make('div', 'CorkDeck ' + version + ' · 设置：' + setup + ' · ');
      meta.className = 'meta';
      const ref = make('a', '原始记录');
      if (source.startsWith('https://github.com/lerosua/CorkDeck/')) ref.href = source;
      meta.append(ref);
      row.append(meta);
      list.append(row);
    }
    entry.append(list);
    games.append(entry);
  }
}
async function load() {
  const res = await fetch('reports.tsv');
  if (!res.ok) throw Error();
  const lines = (await res.text()).trimEnd().split(/\r?\n/);
  if (!lines.shift()?.startsWith('AppID\t')) throw Error();
  reports = lines.map(line => line.split('\t'));
  if (reports.some(row => row.length !== 7)) throw Error();
  for (const value of new Set(reports.map(row => row[3]))) byId('stage').add(new Option(value, value));
  render();
}
byId('query').addEventListener('input', render);
byId('stage').addEventListener('change', render);
load().catch(() => { byId('status').textContent = '列表暂时无法读取，请刷新重试。'; });
