#include "zndNet.h"

namespace ZNDNet
{
ZNDSingle::ZNDSingle(const std::string &servstring, const std::string &name, const std::string &pass, uint32_t max_players)
: ZNDNet(servstring), users(ZNDNET_SES_USERS_MAX)
{
    session.Init(GenerateID(), name, false);
    session.max_players = max_players;
    session.password = pass;
    session.open = true;
}


};
