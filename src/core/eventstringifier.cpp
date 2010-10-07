/***************************************************************************
 *   Copyright (C) 2005-2010 by the Quassel Project                        *
 *   devel@quassel-irc.org                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3.                                           *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "eventstringifier.h"

#include "coresession.h"
#include "messageevent.h"

EventStringifier::EventStringifier(CoreSession *parent) : QObject(parent),
  _coreSession(parent),
  _whois(false)
{

}

void EventStringifier::displayMsg(NetworkEvent *event, Message::Type msgType, const QString &msg, const QString &sender,
                                  const QString &target, Message::Flags msgFlags) {
  if(event->flags().testFlag(EventManager::Silent))
    return;

  MessageEvent *msgEvent = createMessageEvent(event, msgType, msg, sender, target, msgFlags);
  sendMessageEvent(msgEvent);
}

MessageEvent *EventStringifier::createMessageEvent(NetworkEvent *event, Message::Type msgType, const QString &msg, const QString &sender,
                        const QString &target, Message::Flags msgFlags) {
  MessageEvent *msgEvent = new MessageEvent(msgType, event->network(), msg, sender, target, msgFlags);
  msgEvent->setTimestamp(event->timestamp());
  return msgEvent;
}

void EventStringifier::sendMessageEvent(MessageEvent *event) {
  coreSession()->eventManager()->sendEvent(event);
}

void EventStringifier::processIrcEventNumeric(IrcEventNumeric *e) {
  //qDebug() << e->number();
  switch(e->number()) {
  // Welcome, status, info messages. Just display these.
  case 1: case 2: case 3: case 4: case 5:
  case 221: case 250: case 251: case 252: case 253: case 254: case 255: case 265: case 266:
  case 372: case 375:
    displayMsg(e, Message::Server, e->params().join(" "), e->prefix());
    break;

  // Server error messages without param, just display them
  case 409: case 411: case 412: case 422: case 424: case 445: case 446: case 451: case 462:
  case 463: case 464: case 465: case 466: case 472: case 481: case 483: case 485: case 491: case 501: case 502:
  case 431: // ERR_NONICKNAMEGIVEN
    displayMsg(e, Message::Error, e->params().join(" "), e->prefix());
    break;

  // Server error messages, display them in red. First param will be appended.
  case 401: {
    QString target = e->params().takeFirst();
    displayMsg(e, Message::Error, e->params().join(" ") + " " + target, e->prefix(), target, Message::Redirected);
    break;
  }

  case 402: case 403: case 404: case 406: case 408: case 415: case 421: case 442: {
    QString channelName = e->params().takeFirst();
    displayMsg(e, Message::Error, e->params().join(" ") + " " + channelName, e->prefix());
    break;
  }

  // Server error messages which will be displayed with a colon between the first param and the rest
  case 413: case 414: case 423: case 441: case 444: case 461:  // FIXME see below for the 47x codes
  case 467: case 471: case 473: case 474: case 475: case 476: case 477: case 478: case 482:
  case 436: // ERR_NICKCOLLISION
  {
    QString p = e->params().takeFirst();
    displayMsg(e, Message::Error, p + ": " + e->params().join(" "));
    break;
  }

  // Ignore these commands.
  case 321: case 366: case 376:
    break;

  // CAP stuff
  case 903: case 904: case 905: case 906: case 907:
  {
    displayMsg(e, Message::Info, "CAP: " + e->params().join(""));
    break;
  }

  // Everything else will be marked in red, so we can add them somewhere.
  default:
    if(_whois) {
      // many nets define their own WHOIS fields. we fetch those not in need of special attention here:
      displayMsg(e, Message::Server, tr("[Whois] ") + e->params().join(" "), e->prefix());
    } else {
      // FIXME figure out how/where to do this in the future
      //if(coreSession()->ircListHelper()->requestInProgress(network()->networkId()))
      //  coreSession()->ircListHelper()->reportError(params.join(" "));
      //else
        displayMsg(e, Message::Error, QString("%1 %2").arg(e->number(), 3, 10, QLatin1Char('0')).arg(e->params().join(" ")), e->prefix());
    }
  }
}

void EventStringifier::processIrcEventInvite(IrcEvent *e) {
  displayMsg(e, Message::Invite, tr("%1 invited you to channel %2").arg(e->nick(), e->params().at(1)));
}

void EventStringifier::earlyProcessIrcEventKick(IrcEvent *e) {
  IrcUser *victim = e->network()->ircUser(e->params().at(1));
  if(victim) {
    QString channel = e->params().at(0);
    QString msg = victim->nick();
    if(e->params().count() > 2)
      msg += " " + e->params().at(2);

    displayMsg(e, Message::Kick, msg, e->prefix(), channel);
  }
}

// this needs to be called before the ircuser is renamed!
void EventStringifier::earlyProcessIrcEventNick(IrcEvent *e) {
  if(e->params().count() < 1)
    return;

  IrcUser *ircuser = e->network()->updateNickFromMask(e->prefix());
  if(!ircuser) {
    qWarning() << Q_FUNC_INFO << "Unknown IrcUser!";
    return;
  }

  QString newnick = e->params().at(0);
  QString oldnick = ircuser->nick();

  QString sender = e->network()->isMyNick(oldnick) ? newnick : e->prefix();
  foreach(const QString &channel, ircuser->channels())
    displayMsg(e, Message::Nick, newnick, sender, channel);
}

void EventStringifier::earlyProcessIrcEventPart(IrcEvent *e) {
  if(e->params().count() < 1)
    return;

  QString channel = e->params().at(0);
  QString msg = e->params().count() > 1? e->params().at(1) : QString();

  displayMsg(e, Message::Part, msg, e->prefix(), channel);
}

void EventStringifier::processIrcEventPong(IrcEvent *e) {
  QString timestamp = e->params().at(1);
  QTime sendTime = QTime::fromString(timestamp, "hh:mm:ss.zzz");
  if(!sendTime.isValid())
    displayMsg(e, Message::Server, "PONG " + e->params().join(" "), e->prefix());
}

void EventStringifier::processIrcEventTopic(IrcEvent *e) {
  displayMsg(e, Message::Topic, tr("%1 has changed topic for %2 to: \"%3\"")
             .arg(e->nick(), e->params().at(0), e->params().at(1)), QString(), e->params().at(0));
}

void EventStringifier::processIrcEvent301(IrcEvent *e) {
  QString nick = e->params().at(0);
  QString awayMsg = e->params().at(1);
  QString msg, target;
  bool send = true;

  // FIXME: proper redirection needed
  if(_whois) {
    msg = tr("[Whois] ");
  } else {
    target = nick;
    IrcUser *ircuser = e->network()->ircUser(nick);
    if(ircuser) {
      int now = QDateTime::currentDateTime().toTime_t();
      const int silenceTime = 60;
      if(ircuser->lastAwayMessage() + silenceTime >= now)
        send = false;
      ircuser->setLastAwayMessage(now);
    }
  }
  if(send)
    displayMsg(e, Message::Server, msg + tr("%1 is away: \"%2\"").arg(nick, awayMsg), QString(), target);
}

/* RPL_UNAWAY */
void EventStringifier::processIrcEvent305(IrcEvent *e) {
  displayMsg(e, Message::Server, tr("You are no longer marked as being away"));
}

/* RPL_NOWAWAY */
void EventStringifier::processIrcEvent306(IrcEvent *e) {
  if(!e->network()->autoAwayActive())
    displayMsg(e, Message::Server, tr("You have been marked as being away"));
}

/*
WHOIS-Message:
   Replies 311 - 313, 317 - 319 are all replies generated in response to a WHOIS message.
  and 301 (RPL_AWAY)
              "<nick> :<away message>"
WHO-Message:
   Replies 352 and 315 paired are used to answer a WHO message.

WHOWAS-Message:
   Replies 314 and 369 are responses to a WHOWAS message.

*/

/*  RPL_WHOISUSER - "<nick> <user> <host> * :<real name>" */
void EventStringifier::processIrcEvent311(IrcEvent *e) {
  _whois = true;

  const QString whoisUserString = tr("[Whois] %1 is %2 (%3)");

  IrcUser *ircuser = e->network()->ircUser(e->params().at(0));
  if(ircuser)
    displayMsg(e, Message::Server, whoisUserString.arg(ircuser->nick(), ircuser->hostmask(), ircuser->realName()));
  else {
    QString host = QString("%1!%2@%3").arg(e->params().at(0), e->params().at(1), e->params().at(2));
    displayMsg(e, Message::Server, whoisUserString.arg(e->params().at(0), host, e->params().last()));
  }
}

/*  RPL_WHOISSERVER -  "<nick> <server> :<server info>" */
void EventStringifier::processIrcEvent312(IrcEvent *e) {
  if(_whois)
    displayMsg(e, Message::Server, tr("[Whois] %1 is online via %2 (%3)").arg(e->params().at(0), e->params().at(1), e->params().last()));
  else
    displayMsg(e, Message::Server, tr("[Whowas] %1 was online via %2 (%3)").arg(e->params().at(0), e->params().at(1), e->params().last()));
}

/*  RPL_WHOWASUSER - "<nick> <user> <host> * :<real name>" */
void EventStringifier::processIrcEvent314(IrcEvent *e) {
  if(e->params().count() < 3)
    return;

  displayMsg(e, Message::Server, tr("[Whowas] %1 was %2@%3 (%4)").arg(e->params()[0], e->params()[1], e->params()[2], e->params().last()));
}

/*  RPL_ENDOFWHO: "<name> :End of WHO list" */
void EventStringifier::processIrcEvent315(IrcEvent *e) {
  QStringList p = e->params();
  p.takeLast(); // should be "End of WHO list"
  displayMsg(e, Message::Server, tr("[Who] End of /WHO list for %1").arg(p.join(" ")));
}

/*  RPL_WHOISIDLE - "<nick> <integer> :seconds idle"
   (real life: "<nick> <integer> <integer> :seconds idle, signon time) */
void EventStringifier::processIrcEvent317(IrcEvent *e) {
  int idleSecs = e->params()[1].toInt();

  if(e->params().count() > 3) { // if we have more then 3 params we have the above mentioned "real life" situation
    QDateTime loginTime = QDateTime::fromTime_t(e->params()[2].toInt());
    displayMsg(e, Message::Server, tr("[Whois] %1 is logged in since %2").arg(e->params()[0], loginTime.toString()));
  }
  displayMsg(e, Message::Server, tr("[Whois] %1 is idling for %2 (since %3)")
             .arg(e->params()[0], secondsToString(idleSecs), e->timestamp().toLocalTime().addSecs(-idleSecs).toString()));
}

/*  RPL_ENDOFWHOIS - "<nick> :End of WHOIS list" */
void EventStringifier::processIrcEvent318(IrcEvent *e) {
  _whois = false;
  displayMsg(e, Message::Server, tr("[Whois] End of /WHOIS list"));
}

/*  RPL_WHOISCHANNELS - "<nick> :*( ( "@" / "+" ) <channel> " " )" */
void EventStringifier::processIrcEvent319(IrcEvent *e) {
  if(e->params().count() < 2)
    return;

  QString nick = e->params().first();
  QStringList op;
  QStringList voice;
  QStringList user;
  foreach(QString channel, e->params().last().split(" ")) {
    if(channel.startsWith("@"))
       op.append(channel.remove(0,1));
    else if(channel.startsWith("+"))
      voice.append(channel.remove(0,1));
    else
      user.append(channel);
  }
  if(!user.isEmpty())
    displayMsg(e, Message::Server, tr("[Whois] %1 is a user on channels: %2").arg(nick, user.join(" ")));
  if(!voice.isEmpty())
    displayMsg(e, Message::Server, tr("[Whois] %1 has voice on channels: %2").arg(nick, voice.join(" ")));
  if(!op.isEmpty())
    displayMsg(e, Message::Server, tr("[Whois] %1 is an operator on channels: %2").arg(nick, op.join(" ")));
}

/*  RPL_WHOISACCOUNT: "<nick> <account> :is authed as */
void EventStringifier::processIrcEvent330(IrcEvent *e) {
  if(e->params().count() < 3)
    return;

  displayMsg(e, Message::Server, tr("[Whois] %1 is authed as %2").arg(e->params()[0], e->params()[1]));
}

// template
/*

void EventStringifier::processIrcEvent(IrcEvent *e) {

}

*/