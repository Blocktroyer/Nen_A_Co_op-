"""Smoke test for Nen's Supabase backend, run outside Unreal.

Signs in three anonymous players (A, B and an outsider C), pairs A and B,
sends a pigeon and two storms, applies them, and checks the security rules:
partners see each other, C sees nothing, nobody can write the other's data.

    python Tools/backend_smoke_test.py

Reads ProjectUrl / PublishableKey from Config/DefaultGame.ini. Uses only the
standard library. Each run creates 3 anonymous users; delete them under
Authentication -> Users in the Supabase dashboard (their couple, state, mail
and events are removed with them).
"""
import json
import os
import re
import sys
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INI = open(os.path.join(ROOT, 'Config', 'DefaultGame.ini'), encoding='utf-8-sig').read()
URL = re.search(r'^ProjectUrl=(.+)$', INI, re.M).group(1).strip().strip('"').rstrip('/')
KEY = re.search(r'^PublishableKey=(.+)$', INI, re.M).group(1).strip().strip('"')

results = []


def call(method, path, body=None, token=None, prefer=None):
    """Returns (status, parsed_json_or_text)."""
    headers = {'apikey': KEY, 'Content-Type': 'application/json'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    if prefer:
        headers['Prefer'] = prefer
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(URL + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            status, raw = r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        status, raw = e.code, e.read().decode()
    try:
        return status, json.loads(raw) if raw else None
    except json.JSONDecodeError:
        return status, raw


def check(name, ok, detail=''):
    results.append(ok)
    print(('PASS  ' if ok else 'FAIL  ') + name + ('' if ok else f'\n      -> {detail}'))
    return ok


def sign_in(label):
    status, res = call('POST', '/auth/v1/signup', {})
    if status != 200 or not isinstance(res, dict) or 'access_token' not in res:
        print(f'Could not sign in player {label}: HTTP {status} {res}')
        print('Is "Allow anonymous sign-ins" enabled under Authentication -> Sign In / Providers?')
        sys.exit(1)
    return res['access_token'], res['user']['id']


def rpc(fn, body, token):
    return call('POST', '/rest/v1/rpc/' + fn, body, token)


def main():
    print(f'Testing {URL}\n')
    a_tok, a_id = sign_in('A')
    b_tok, b_id = sign_in('B')
    c_tok, c_id = sign_in('C')
    check('three anonymous players signed in', True)

    # --- pairing
    status, code = rpc('create_couple', {}, a_tok)
    if not check('A creates a couple and gets an invite code', status == 200 and isinstance(code, str),
                 f'HTTP {status} {code}'):
        print('\nThe tables / functions are probably missing: run the SQL scripts first.')
        sys.exit(1)
    status, res = rpc('join_couple', {'p_code': code.lower()}, b_tok)
    check('B joins with the code (case-insensitive)', status == 200, f'HTTP {status} {res}')
    status, res = rpc('join_couple', {'p_code': code}, c_tok)
    check('C cannot join a full couple', status >= 400, f'HTTP {status} {res}')
    status, res = rpc('create_couple', {}, b_tok)
    check('B cannot start a second couple', status >= 400, f'HTTP {status} {res}')

    # --- visibility of player_state
    status, rows = call('GET', '/rest/v1/player_state?select=player_id', token=a_tok)
    seen = {r['player_id'] for r in rows} if status == 200 else set()
    check('A sees own + partner state (2 rows)', seen == {a_id, b_id}, f'HTTP {status} {rows}')
    status, rows = call('GET', '/rest/v1/player_state?select=player_id', token=c_tok)
    check('C sees no player state', status == 200 and rows == [], f'HTTP {status} {rows}')
    status, rows = call('GET', '/rest/v1/couples?select=id', token=c_tok)
    check('C sees no couples', status == 200 and rows == [], f'HTTP {status} {rows}')

    # --- writing player_state
    status, rows = call('PATCH', f'/rest/v1/player_state?player_id=eq.{a_id}',
                        {'hope': 5, 'checkpoint': 1}, a_tok, prefer='return=representation')
    check('A updates own hope/checkpoint', status == 200 and len(rows) == 1 and rows[0]['hope'] == 5,
          f'HTTP {status} {rows}')
    status, rows = call('PATCH', f'/rest/v1/player_state?player_id=eq.{a_id}',
                        {'hope': 999}, b_tok, prefer='return=representation')
    check("B cannot change A's state", status in (200, 204) and not rows or status >= 400, f'HTTP {status} {rows}')
    status, rows = call('PATCH', f'/rest/v1/player_state?player_id=eq.{b_id}',
                        {'couple_id': '00000000-0000-0000-0000-000000000000'}, b_tok)
    check('B cannot change own couple_id (column locked)', status >= 400, f'HTTP {status} {rows}')

    # --- pigeon mail
    status, res = rpc('send_pigeon', {'p_message': 'miss you', 'p_items': ['pebble']}, a_tok)
    check('A sends a pigeon', status == 200 and isinstance(res, dict), f'HTTP {status} {res}')
    mail_q = f'/rest/v1/mailbox?recipient=eq.{b_id}&collected_at=is.null&order=deliver_at'
    mail_status, mail = call('GET', mail_q, token=b_tok)
    check('B receives it instantly',
          mail_status == 200 and len(mail) == 1 and mail[0]['message'] == 'miss you',
          f'HTTP {mail_status} {mail}')
    status, rows = call('GET', '/rest/v1/mailbox?select=id', token=c_tok)
    check('C cannot read any mail', status == 200 and rows == [], f'HTTP {status} {rows}')
    status, res = rpc('send_pigeon', {'p_message': 'hi?'}, c_tok)
    check('C (unpaired) cannot send a pigeon', status >= 400, f'HTTP {status} {res}')
    if mail_status == 200 and isinstance(mail, list) and mail:
        rpc('mark_mail_collected', {'p_ids': [mail[0]['id']]}, c_tok)
        status, still = call('GET', mail_q, token=b_tok)
        check("C cannot mark B's mail as read", len(still) == 1, f'{still}')
        rpc('mark_mail_collected', {'p_ids': [mail[0]['id']]}, b_tok)
        status, after = call('GET', mail_q, token=b_tok)
        check('B marks it read, inbox empties', status == 200 and after == [], f'HTTP {status} {after}')

    # --- world events (storms stack)
    for _ in range(2):
        status, res = rpc('send_world_event', {'p_type': 'water_stream', 'p_target': 'Ravine',
                                               'p_payload': {'v': 1, 'delta_flow': 1.0, 'cause': 'storm'}}, a_tok)
    check('A sends two storms to Ravine', status == 200, f'HTTP {status} {res}')
    ev_q = f'/rest/v1/world_events?recipient=eq.{b_id}&applied_at=is.null&order=created_at'
    status, events = call('GET', ev_q, token=b_tok)
    check('B has 2 pending events', status == 200 and len(events) == 2, f'HTTP {status} {events}')
    status, rows = call('GET', '/rest/v1/world_events?select=id', token=c_tok)
    check('C cannot read any events', status == 200 and rows == [], f'HTTP {status} {rows}')

    if not (status == 200 and isinstance(events, list)):
        events = []
    flow = sum(e['payload'].get('delta_flow', 0) for e in events)
    world = {'v': 1, 'levels': {'Ravine': {'streamFlow': flow}}, 'checkpoints': {}}
    status, res = rpc('save_world', {'p_world': world, 'p_applied': [e['id'] for e in events]}, b_tok)
    check('B saves world + acknowledges events in one call', status in (200, 204), f'HTTP {status} {res}')
    status, events = call('GET', ev_q, token=b_tok)
    check('no pending events left', status == 200 and events == [], f'HTTP {status} {events}')
    status, rows = call('GET', f'/rest/v1/player_state?select=world&player_id=eq.{b_id}', token=a_tok)
    got = rows[0]['world'].get('levels', {}).get('Ravine', {}).get('streamFlow') if status == 200 and rows else None
    check("B's Ravine stream flow is 2 (visible to partner A)", got == 2, f'HTTP {status} {rows}')

    passed = sum(results)
    print(f'\n{passed}/{len(results)} checks passed')
    print(f'Test players: A={a_id}  B={b_id}  C={c_id}')
    sys.exit(0 if passed == len(results) else 1)


if __name__ == '__main__':
    main()
