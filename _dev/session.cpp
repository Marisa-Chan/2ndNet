#include "zndNet.h"
#include <regex>

namespace ZNDNet
{

SessionInfo& SessionInfo::operator= (const SessionInfo& x)
{
    ID = x.ID;
    name = x.name;
    pass = x.pass;
    players = x.players;
    max_players = x.max_players;
    return *this;
}

UserInfo& UserInfo::operator= (const UserInfo& x)
{
    ID = x.ID;
    name = x.name;
    lead = x.lead;
    return *this;
}

NetSession::NetSession()
{
    clear();
}

void NetSession::clear()
{
    ID = 0;
    name.clear();
    users.clear();
    password.clear();
    lead = NULL;
    lobby = false;
    max_players = 0;
    orphanedTimer = 0;
    open = true;
}

void NetSession::Init(uint64_t _ID, const std::string &_name, bool _lobby)
{
    ID = _ID;
    name = _name;
    users.clear();
    password.clear();
    lead = NULL;
    lobby = _lobby;
    max_players = 0;
    open = true;
}

bool NetSession::HasSlot()
{
    if (!max_players || users.size() < max_players)
        return true;

    return false;
}

NetSession *ZNDNet::Srv_SessionFind(uint64_t _ID)
{
    if (_ID == sLobby.ID)
        return &sLobby;

    NetSessionMap::iterator fnd = sessions.find(_ID);
    if (fnd != sessions.end())
        return fnd->second;

    return NULL;
}

NetSession *ZNDNet::Srv_SessionFind(const std::string &name)
{
    for(NetSessionMap::iterator it = sessions.begin(); it != sessions.end(); it++)
    {
        if (it->second->name == name)
            return it->second;
    }
    return NULL;
}

void ZNDNet::Srv_SessionBroadcast(NetSession *ses, RefData *dat, uint8_t flags, uint8_t chnl, NetUser *from)
{
    if (!ses || !dat)
        return;

    if (SDL_LockMutex(sendPktListMutex) == 0)
    {
        for(NetUserList::iterator it = ses->users.begin(); it != ses->users.end(); it++)
        {
            NetUser *usr = *it;
            if (usr && usr != from && usr->status != STATUS_DISCONNECTED)
            {
                SendingData *dta = new SendingData(usr->addr, usr->GetSeq(), dat, flags);
                dta->SetChannel(usr->__idx, chnl);
                sendPktList.push_back(dta);
            }
        }
        SDL_UnlockMutex(sendPktListMutex);
    }
}

void ZNDNet::Srv_SessionListUsers(NetUser *usr)
{
    if (!usr)
        return;

    if (usr->status == STATUS_DISCONNECTED || usr->sesID == 0 || usr->sesID == sLobby.ID)
        return;

    NetSession *ses = Srv_SessionFind(usr->sesID);
    if (ses && !ses->lobby)
    {
        RefDataWStream *dat = RefDataWStream::create();
        dat->writeU8(USR_MSG_SES_USERLIST);
        dat->writeU32(ses->users.size());

        for(NetUserList::iterator it = ses->users.begin(); it != ses->users.end(); it++)
        {
//            if ( ses->lead ==  (*it))
//                dat->writeU8(1);
//            else
//                dat->writeU8(0);
            dat->writeU64( (*it)->ID );
            dat->writeSzStr( (*it)->name );
        }

        Send_PushData( MkSendingData(usr, dat, 0, 0) ); //Send it in sync System channel
    }
}


void ZNDNet::Srv_SessionUserLeave(NetUser *usr)
{
    if (!usr)
        return;

    NetSession *sold = Srv_SessionFind(usr->sesID);

    if (sold)
    {
        if (sold->lobby)
            sold->users.remove(usr);
        else
        {
            sold->users.remove(usr); // Remove this luser

            if (!sold->users.empty()) // If anybody is alive
            {
                // Make packets for another users about usr is leave
                RefData *datLeave = Srv_USRDataGenUserLeave(usr);
                Srv_SessionBroadcast(sold, datLeave, 0, 0, usr); //Send it in sync System channel
            }

            if (sold->lead == usr) // Oh my... this luser was a leader
            {
                if (!sold->users.empty())
                {
                    sold->lead = sold->users.front();

                    // Make packet for usr with leader flag
                    Srv_SendLeaderStatus(sold->lead, true);
                }
                else
                {
                    sold->orphanedTimer = ttime.GetTicks() + TIMEOUT_SESSION; // Death timer, tick-tack-tick-tack, time is coming
                    sold->lead = NULL;
                }
            }
        }
    }
}

void ZNDNet::Srv_DoSessionUserJoin(NetUser *usr, NetSession *ses)
{
    if (!usr || !ses)
        return;

    Srv_SessionUserLeave(usr);

    if (ses->ID != sLobby.ID)
    {
        if (!ses->users.empty())
        {
            RefData *dat = Srv_USRDataGenUserJoin(usr);
            Srv_SessionBroadcast(ses, dat, 0, 0, usr); //Send it in sync System channel
        }

        if (!ses->lead)
        {
            ses->lead = usr;
            Srv_SendSessionJoin(usr, ses, true);
        }
        else
            Srv_SendSessionJoin(usr, ses, false);
    }

    usr->sesID = ses->ID;
    ses->users.push_back(usr);

    Srv_SessionListUsers(usr);
}

RefData *ZNDNet::Srv_SessionErr(uint8_t code)
{
    RefDataStatic *dt = RefDataStatic::create(2);
    uint8_t *data = dt->get();
    data[0] = SYS_MSG_SES_ERR;
    data[1] = code;
    return dt;
}

void ZNDNet::Srv_SessionErrSend(NetUser *usr, uint8_t code)
{
    if (!usr)
        return;

    RefData *msg = Srv_SessionErr(code);
    SendingData *snd = new SendingData(usr->addr, usr->GetSeq(), msg, PKT_FLAG_SYSTEM);
    snd->SetChannel(usr->__idx, 0);
    Send_PushData(snd);
}

bool ZNDNet::SessionCheckName(const std::string &name)
{
    if (name.size() > SES_NAME_MAX)
        return false;

    std::regex re("[[:alnum:]][[:print:]]+[[:graph:]]" );

    if ( std::regex_match (name,  re) )
        return true;

    return false;
}

bool ZNDNet::SessionCheckPswd(const std::string &pswd)
{
    std::regex re("[[:graph:]]{4,16}");

    if ( std::regex_match (pswd,  re) )
        return true;

    return false;
}

}
