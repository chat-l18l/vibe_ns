// OETELX Chat — minimal web client for the netserver HTTP API.
// REST for rooms/history/posting, Server-Sent Events for live updates.
'use strict';

const $ = (id) => document.getElementById(id);
const api = (p, opt) => fetch(p, opt).then((r) => r.json());

let rooms = [];
let current = null;   // current room id
let cursor = 0;       // highest message id rendered
let events = null;    // EventSource

const nameInput = $('name');
nameInput.value = localStorage.getItem('chat_name') || '';

function fmtTime(ts) {
  const d = new Date(ts * 1000);
  return d.toISOString().substr(11, 5);   // HH:MM (UTC, matches telnet)
}

function addMessage(m) {
  if (m.id <= cursor) return;
  cursor = m.id;
  const div = document.createElement('div');
  div.className = 'msg';
  div.innerHTML =
    `<span class="t">[${fmtTime(m.ts)}]</span> ` +
    `<span class="u"></span>: <span class="b"></span>`;
  div.querySelector('.u').textContent = m.user;
  div.querySelector('.b').textContent = m.body;   // textContent = no HTML injection
  const log = $('log');
  const atBottom = log.scrollHeight - log.scrollTop - log.clientHeight < 40;
  log.appendChild(div);
  if (atBottom) log.scrollTop = log.scrollHeight;
}

async function refreshRooms() {
  rooms = await api('/api/rooms');
  const box = $('rooms');
  box.innerHTML = '';
  for (const r of rooms) {
    const el = document.createElement('div');
    el.className = 'room' + (r.id === current ? ' active' : '');
    el.innerHTML = `#${r.name} <small></small>`;
    el.onclick = () => selectRoom(r.id);
    box.appendChild(el);
    api(`/api/rooms/${r.id}/users`).then((u) => {
      el.querySelector('small').textContent = `(${u.count})`;
    });
  }
}

async function selectRoom(id) {
  current = id;
  cursor = 0;
  $('log').innerHTML = '';
  const room = rooms.find((r) => r.id === id);
  $('title').textContent = '#' + (room ? room.name : id);
  refreshRooms();
  updateWho();

  // history first, then live stream from the newest id we have
  const history = await api(`/api/rooms/${id}/messages?limit=50`);
  history.forEach(addMessage);

  if (events) events.close();
  const who = encodeURIComponent(nameInput.value.trim());
  events = new EventSource(`/api/events/rooms/${id}?user=${who}`);
  events.onmessage = (e) => { addMessage(JSON.parse(e.data)); updateWho(); };
}

async function updateWho() {
  if (current == null) return;
  const u = await api(`/api/rooms/${current}/users`);
  $('who').textContent = u.count ? `${u.count} online: ${u.users.join(', ')}` : '';
}

$('bar').onsubmit = async (e) => {
  e.preventDefault();
  const user = nameInput.value.trim();
  const body = $('text').value.trim();
  if (!user || !body || current == null) return;
  localStorage.setItem('chat_name', user);
  $('text').value = '';
  await fetch(`/api/rooms/${current}/messages`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ user, body }),
  });
};

(async function init() {
  await refreshRooms();
  if (rooms.length) selectRoom(rooms[0].id);
  setInterval(updateWho, 15000);
})();
