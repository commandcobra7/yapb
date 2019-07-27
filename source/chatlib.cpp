//
// Yet Another POD-Bot, based on PODBot by Markus Klinge ("CountFloyd").
// Copyright (c) YaPB Development Team.
//
// This software is licensed under the BSD-style license.
// Additional exceptions apply. For full license details, see LICENSE.txt or visit:
//     https://yapb.ru/license
//

#include <yapb.h>

ConVar yb_chat ("yb_chat", "1");

void BotUtils::stripTags (String &line) {
   if (line.empty ()) {
      return;
   }

   for (const auto &tag : m_tags) {
      const size_t start = line.find (tag.first, 0);

      if (start != String::kInvalidIndex) {
         const size_t end = line.find (tag.second, start);
         const size_t diff = end - start;

         if (end != String::kInvalidIndex && end > start && diff < 32 && diff > 4) {
            line.erase (start, diff + tag.second.length ());
            break;
         }
      }
   }
}

void BotUtils::humanizePlayerName (String &playerName) {
   if (playerName.empty ()) {
      return;
   }

   // drop tag marks, 80 percent of time
   if (rg.chance (80)) {
      stripTags (playerName);
   }
   else {
      playerName.trim ();
   }

   // sometimes switch name to lower characters, only valid for the english languge
   if (rg.chance (8) && strcmp (yb_language.str (), "en") == 0) {
      playerName.lowercase ();
   }
}

void BotUtils::addChatErrors (String &line) {
   // sometimes switch name to lower characters, only valid for the english languge
   if (rg.chance (8) && strcmp (yb_language.str (), "en") == 0) {
      line.lowercase ();
   }
   auto length = line.length ();

   if (length > 15) {
      size_t percentile = line.length () / 2;

      // "length / 2" percent of time drop a character
      if (rg.chance (percentile)) {
         line.erase (rg.int_ (length / 8, length - length / 8), 1);
      }

      // "length" / 4 precent of time swap character
      if (rg.chance (percentile / 2)) {
         size_t pos = rg.int_ (length / 8, 3 * length / 8); // choose random position in string
         cr::swap (line[pos], line[pos + 1]);
      }
   }
}

bool BotUtils::checkKeywords (const String &line, String &reply) {
   // this function checks is string contain keyword, and generates reply to it

   if (!yb_chat.bool_ () || line.empty ()) {
      return false;
   }

   for (auto &factory : conf.getReplies ()) {
      for (const auto &keyword : factory.keywords) {

         // check is keyword has occurred in message
         if (line.find (keyword) != String::kInvalidIndex) {
            StringArray &usedReplies = factory.usedReplies;

            if (usedReplies.length () >= factory.replies.length () / 4) {
               usedReplies.clear ();
            }

            if (!factory.replies.empty ()) {
               bool replyUsed = false;
               const String &choosenReply = factory.replies.random ();

               // don't say this twice
               for (auto &used : usedReplies) {
                  if (used.contains (choosenReply)) {
                     replyUsed = true;
                     break;
                  }
               }

               // reply not used, so use it
               if (!replyUsed) {
                  reply.assign (choosenReply); // update final buffer
                  usedReplies.push (choosenReply); // add to ignore list
                  return true;
               }
            }
         }
      }
   }
   // didn't find a keyword? 70% of the time use some universal reply
   if (rg.chance (70) && conf.hasChatBank (Chat::NoKeyword)) {
      reply.assign (conf.pickRandomFromChatBank (Chat::NoKeyword));
      return true;
   }
   return false;
}

void Bot::prepareChatMessage (const String &message) {
   // this function parses messages from the botchat, replaces keywords and converts names into a more human style

   if (!yb_chat.bool_ () || message.empty ()) {
      return;
   }
   m_chatBuffer.assign (message.chars ());

   // must be called before return or on the end
   auto finishPreparation = [&] () {
      if (!m_chatBuffer.empty ()) {
         util.addChatErrors (m_chatBuffer);
      }
   };

   // need to check if we're have special symbols
   size_t pos = message.find ('%');

   // nothing found, bail out
   if (pos == String::kInvalidIndex || pos >= message.length ()) {
      finishPreparation ();
      return;
   }

   // get the humanized name out of client
   auto humanizedName = [] (int index) -> String {
      auto ent = game.playerOfIndex (index);

      if (!util.isPlayer (ent)) {
         return "unknown";
      }
      String playerName = STRING (ent->v.netname);
      util.humanizePlayerName (playerName);

      return playerName;
   };

   // find highfrag player
   auto getHighfragPlayer = [&] () -> String {
      int highestFrags = -1;
      int index = 0;

      for (int i = 0; i < game.maxClients (); ++i) {
         const Client &client = util.getClient (i);

         if (!(client.flags & ClientFlags::Used) || client.ent == ent ()) {
            continue;
         }
         int frags = static_cast <int> (client.ent->v.frags);

         if (frags > highestFrags) {
            highestFrags = frags;
            index = i;
         }
      }
      return humanizedName (index);
   };

   // get roundtime
   auto getRoundTime = [] () -> String {
      auto roundTimeSecs = static_cast <int> (bots.getRoundEndTime () - game.timebase ());
      
      String roundTime;
      roundTime.assignf ("%02d:%02d", cr::clamp (roundTimeSecs / 60, 0, 59), cr::clamp (cr::abs (roundTimeSecs % 60), 0, 59));

      return roundTime;
   };

   // get bot's victim
   auto getMyVictim = [&] () -> String {;
      return humanizedName (game.indexOfPlayer (m_lastVictim));
   };

   // get the game name alias
   auto getGameName = [] () -> String {
      String gameName;

      if (game.is (GameFlags::ConditionZero)) {
         if (rg.chance (30)) {
            gameName = "CZ";
         }
         else {
            gameName = "Condition Zero";
         }
      }
      else if (game.is (GameFlags::Modern) || game.is (GameFlags::Legacy)) {
         if (rg.chance (30)) {
            gameName = "CS";
         }
         else {
            gameName = "Counter-Strike";
         }
      }
      return gameName;
   };

   // get enemy or teammate alive
   auto getPlayerAlive = [&] (bool needsEnemy) -> String {
      for (const auto &client : util.getClients ()) {
         if (!(client.flags & ClientFlags::Used) || !(client.flags & ClientFlags::Alive) || client.ent == ent ()) {
            continue;
         }

         if (needsEnemy && m_team != client.team) {
            return humanizedName (game.indexOfPlayer (client.ent));
         }
         else if (!needsEnemy && m_team == client.team) {
            return humanizedName (game.indexOfPlayer (client.ent));
         }
      }
      return "UnknowPA";
   };
   size_t replaceCounter = 0;

   while (replaceCounter < 6 && (pos = m_chatBuffer.find ('%')) != String::kInvalidIndex) {
      // found one, let's do replace
      switch (message[pos + 1]) {

         // the highest frag player
      case 'f':
         m_chatBuffer.replace ("%f", getHighfragPlayer ());
         break;

         // current map name
      case 'm':
         m_chatBuffer.replace ("%m", game.getMapName ());
         break;

         // round time
      case 'r':
         m_chatBuffer.replace ("%r", getRoundTime ());
         break;

         // chat reply
      case 's':
         if (m_sayTextBuffer.entityIndex != -1) {
            m_chatBuffer.replace ("%s", humanizedName (m_sayTextBuffer.entityIndex));
         }
         else {
            m_chatBuffer.replace ("%s", getHighfragPlayer ());
         }
         break;

         // last bot victim
      case 'v':
         m_chatBuffer.replace ("%v", getMyVictim ());
         break;

         // game name
      case 'd':
         m_chatBuffer.replace ("%d", getGameName ());
         break;

         // teammate alive
      case 't':
         m_chatBuffer.replace ("%t", getPlayerAlive (false));
         break;

         // enemy alive
      case 'e':
         m_chatBuffer.replace ("%e", getPlayerAlive (true));
         break;
      };
      replaceCounter++;
   }
   finishPreparation ();
}

bool Bot::checkChatKeywords (String &reply) {
   // this function parse chat buffer, and prepare buffer to keyword searching

   String message = m_sayTextBuffer.sayText;
   return util.checkKeywords (message.uppercase (), reply);
}

bool Bot::isReplyingToChat () {
   // this function sends reply to a player

   if (m_sayTextBuffer.entityIndex != -1 && !m_sayTextBuffer.sayText.empty ()) {
      // check is time to chat is good
      if (m_sayTextBuffer.timeNextChat < game.timebase () + rg.float_ (m_sayTextBuffer.chatDelay / 2, m_sayTextBuffer.chatDelay)) {
         String replyText;

         if (rg.chance (m_sayTextBuffer.chatProbability + rg.int_ (20, 50)) && checkChatKeywords (replyText)) {
            prepareChatMessage (replyText);
            pushMsgQueue (BotMsg::Say);
  
            m_sayTextBuffer.entityIndex = -1;
            m_sayTextBuffer.timeNextChat = game.timebase () + m_sayTextBuffer.chatDelay;
            m_sayTextBuffer.sayText.clear ();

            return true;
         }
         m_sayTextBuffer.entityIndex = -1;
         m_sayTextBuffer.sayText.clear ();
      }
   }
   return false;
}

void Bot::checkForChat () {

   // say a text every now and then
   if (rg.chance (30) || m_notKilled || !yb_chat.bool_ ()) {
      return;
   }

   // bot chatting turned on?
   if (m_lastChatTime + rg.float_ (6.0f, 10.0f) < game.timebase () && bots.getLastChatTimestamp () + rg.float_ (2.5f, 5.0f) < game.timebase () && !isReplyingToChat ()) {
      if (conf.hasChatBank (Chat::Dead)) {
         const auto &phrase = conf.pickRandomFromChatBank (Chat::Dead);
         bool sayBufferExists = false;

         // search for last messages, sayed
         for (auto &sentence : m_sayTextBuffer.lastUsedSentences) {
            if (strncmp (sentence.chars (), phrase.chars (), sentence.length ()) == 0) {
               sayBufferExists = true;
               break;
            }
         }

         if (!sayBufferExists) {
            prepareChatMessage (phrase);
            pushMsgQueue (BotMsg::Say);
 
            m_lastChatTime = game.timebase ();
            bots.setLastChatTimestamp (game.timebase ());

            // add to ignore list
            m_sayTextBuffer.lastUsedSentences.push (phrase);
         }
      }

      // clear the used line buffer every now and then
      if (static_cast <int> (m_sayTextBuffer.lastUsedSentences.length ()) > rg.int_ (4, 6)) {
         m_sayTextBuffer.lastUsedSentences.clear ();
      }
   }
}

void Bot::say (const char *text) {
   // this function prints saytext message to all players

   if (util.isEmptyStr (text) || !yb_chat.bool_ ()) {
      return;
   }
   game.botCommand (ent (), "say \"%s\"", text);
}

void Bot::sayTeam (const char *text) {
   // this function prints saytext message only for teammates

   if (util.isEmptyStr (text) || !yb_chat.bool_ ()) {
      return;
   }
   game.botCommand (ent (), "say_team \"%s\"", text);
}
