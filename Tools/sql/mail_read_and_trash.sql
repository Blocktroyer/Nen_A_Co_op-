-- Mail: keep read letters, add a trash, and make room for To / Body / From.
-- Run once in the Supabase SQL editor (safe to re-run).
--
-- Letters stay in the mailbox table after they're read. The game shows sent and received
-- letters together in one list, and each player has their own trash:
--   collected_at       set when the recipient opens it (the "unread" icon is collected_at is null)
--   trashed_at         set when the recipient throws it away, cleared when they restore it
--   sender_trashed_at  the same, for the sender's copy
-- Both players can already read the letter (policy "arrived mail and mail I sent").

alter table public.mailbox add column if not exists trashed_at timestamptz;
alter table public.mailbox add column if not exists sender_trashed_at timestamptz;

-- message now holds {"v":1,"to":"...","body":"...","from":"..."}, so allow longer text
alter table public.mailbox drop constraint if exists mailbox_message_check;
alter table public.mailbox add constraint mailbox_message_check check (char_length(message) <= 4000);

-- Throw letters away (p_trashed = true) or take them back out of the trash (false).
-- Only moves them in the caller's own trash; the other player's copy is untouched.
create or replace function public.set_mail_trashed(p_ids uuid[], p_trashed boolean default true)
returns void
language sql security definer set search_path = '' as $$
  update public.mailbox
     set trashed_at = case when recipient = auth.uid()
                           then case when p_trashed then coalesce(trashed_at, now()) end
                           else trashed_at end,
         sender_trashed_at = case when sender = auth.uid()
                           then case when p_trashed then coalesce(sender_trashed_at, now()) end
                           else sender_trashed_at end
   where id = any(p_ids)
     and (sender = auth.uid() or (recipient = auth.uid() and deliver_at <= now()))
$$;

revoke execute on function public.set_mail_trashed(uuid[], boolean) from public, anon;
grant execute on function public.set_mail_trashed(uuid[], boolean) to authenticated;
