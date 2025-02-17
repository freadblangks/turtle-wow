/*
 * Copyright (C) 2017-2018 namreeb (legal@namreeb.org)
 *
 * This is private software and may not be shared under any circumstances,
 * absent permission of namreeb.
 */

#ifndef __LIBANTICHEAT_HPP_
#define __LIBANTICHEAT_HPP_

#include "Anticheat.h"
#include "priority.hpp"

#include "Policies/Singleton.h"

#include <memory>
#include <atomic>
#include <mutex>

#if PLATFORM == PLATFORM_WINDOWS
  #define _LIB_ANTICHEAT_CONFIG  "anticheat.conf"
#else
  #define _LIB_ANTICHEAT_CONFIG  SYSCONFDIR "anticheat.conf"
#endif

class WorldSession;
class Warden;
struct AreaEntry;

namespace Anticheat
{
class Movement;

enum CheatType
{
    CHEAT_TYPE_WALL_CLIMB = 0,
    CHEAT_TYPE_WATER_WALK,
    CHEAT_TYPE_SLOW_FALL,
    CHEAT_TYPE_FLY_HACK,
    CHEAT_TYPE_FORBIDDEN,
    CHEAT_TYPE_MULTIJUMP,
    CHEAT_TYPE_TIME_BACK,
    CHEAT_TYPE_OVERSPEED_JUMP,
    CHEAT_TYPE_JUMP_SPEED_CHANGE,
    CHEAT_TYPE_NULL_CLIENT_TIME,
    CHEAT_TYPE_ROOT_MOVE,
    CHEAT_TYPE_EXPLORE,
    CHEAT_TYPE_EXPLORE_HIGH_LEVEL,
    CHEAT_TYPE_OVERSPEED_Z,
    CHEAT_TYPE_SKIPPED_HEARTBEATS,
    CHEAT_TYPE_NUM_DESYNC,
    CHEAT_TYPE_FAKE_TRANSPORT,
    CHEAT_TYPE_TELE_TO_TRANSPORT,
    CHEAT_TYPE_TELEPORT,
    CHEAT_TYPE_TELEPORT_FAR,
    CHEAT_TYPE_FIXED_Z,
    CHEAT_TYPE_BAD_ORDER_ACK,
    CHEAT_TYPE_WARDEN,
    CHEATS_COUNT
};

class SessionAnticheat final : public SessionAnticheatInterface
{
    /*** BEGIN PUBLIC INTERFACE PORTION ***/
    public:
        SessionAnticheat(WorldSession *session, const BigNumber &K);

        void Update(uint32 diff) override;

        // character enum packet has been built and is ready to send
        void SendCharEnum(WorldPacket &&packet) override;

        void NewPlayer() override;
        void LeaveWorld() override;
        void Disconnect() override;

        // addon checksum verification (and fingerprinting ;))))
        bool ReadAddonInfo(WorldPacket *, WorldPacket &) override;
        uint32 GetFingerprint() const override { return _fingerprint; }

        void SendPlayerInfo(ChatHandler *) const override;

        // miscellaneous/generic anticheat detection from the core.  also from within the module, once an action has been determined
        void RecordCheat(uint32 actionMask, const char *detector, const char *format, ...) override;

        // movement cheats
        // TODO: Mark these as const where possible
        bool Movement(MovementInfo &mi, const WorldPacket &packet) override;
        void TimeSkipped(const ObjectGuid &mover, uint32 ms) override;
        bool ExtrapolateMovement(MovementInfo const& mi, uint32 diffMs, Position &pos) override;
        bool SpeedChangeAck(MovementInfo &mi, const WorldPacket &packet, float newSpeed) override;
        bool IsInKnockBack() const override;
        void KnockBack(float speedxy, float speedz, float cos, float sin) override;
        void OnExplore(const AreaEntry *p) override;
        void Teleport(const Position &pos) override;

        void OrderSent(uint16 opcode, uint32 counter) override;
        void OrderAck(uint16 opcode, uint32 counter) override;

        // warden
        void WardenPacket(WorldPacket &packet) override;

    /*** END PUBLIC INTERFACE PORTION ***/

    private:
        static constexpr uint32 AnticheatUpdateInterval = 4000;

        WorldSession * const _session;
        std::unique_ptr<Warden> _warden;

        bool _inWorld;
        uint32 _worldEnterTime;

        // anticheat tick timer, for grouping batches of recent detections
        uint32 _tickTimer;

        // mask of cheat types reported to online GMs during this anticheat tick
        uint32 _cheatsReported;

        // STATS
        uint32 _cheatOccuranceTick[CHEATS_COUNT];    // per anticheat tick (not world/map tick)
        uint32 _cheatOccuranceTotal[CHEATS_COUNT];

        // time remaining until this session is kicked as a result of cheat detection
        uint32 _kickTimer;

        // time remaining until this account is banned as a result of cheat detection
        uint32 _banTimer;

        // when the ban timer expires, do we ban the account?
        bool _banAccount;

        // when the ban timer expires, do we ban the ip?
        bool _banIP;

        // unique fingerprint for this copy of the client
        uint32 _fingerprint;

        // this class handles detection of movement related cheats.  it is split into its own class
        // for readability purposes, but also because not all sessions have a player (and therefore
        // movement) possible; i.e. a session on the login screen, or transferring between maps.
        std::unique_ptr<Anticheat::Movement> _movementData;

        // called whenever the player enters the world (after the client has finished loading)
        void EnterWorld();

        // delayed kick when cheat is detected an kick action is identified
        void BeginKickTimer();

        // delayed ban (account and/or ip) when cheat is detected an ban action is identified
        void BeginBanTimer(bool account, bool ip);

    public:
        // this is down here because a destructor is NOT required by the interface
        ~SessionAnticheat();

        // chat handlers
        void SendCheatInfo(ChatHandler *handler) const;

        float GetDistanceTraveled() const;

        bool GetMovementSpeeds(float *speeds) const;
        bool VerifyTracking(uint16 unitTracking, uint16 resourceTracking) const;
        // XXX: this only temporarily needs removeFlags for logging and authenticity purposes
        void VerifyMovementFlags(uint32 flags, uint32 &removeFlags, bool strict) const;

        // called when the module detects a cheat.  this will apply the configuration to determine
        // appropriate action(s), and call the RecordCheat() above for their execution.
        void RecordCheatInternal(CheatType type, const char *format = nullptr, ...);

        bool IsKickTimerActive() const { return !!_kickTimer; }
        bool IsBanTimerActive() const { return !!_banTimer; }

        // delete old fingerprint usage records
        void CleanupFingerprintHistory() const;

        size_t PendingOrderCount() const;
        uint32 GetWorldEnterTime() const { return _worldEnterTime; }

        // XXX debug logging
        void GetMovementDebugString(std::string &out) const;
};

class AnticheatLib : public AnticheatLibInterface
{
    private:
        std::atomic_bool _extrapDebugActive;
        uint32 _extrapDebugTimer;

        std::mutex _extrapMutex;

        struct ExtrapDebugPoint
        {
            MovementInfo start;
            MovementInfo theirEnd;
            Position extrapEnd;
            float speed1;
            float speed2;
            float errorDistance;
        };

        nam::priority<ExtrapDebugPoint, float, 15> _extrapPoints;

    public:
        virtual void Reload();
        virtual void Initialize();

        virtual std::unique_ptr<SessionAnticheatInterface> NewSession(WorldSession *, const BigNumber &K);

        // anti spam
        AntispamInterface* GetAntispam() const override;

        void EnableExtrapolationDebug(uint32 seconds);
        void OfferExtrapolationData(
            const MovementInfo &start, float speed1, float speed2,
            const MovementInfo &theirEnd, const Position &extrapEnd, float errorDistance);

        //virtual bool ChatCommand(ChatHandler *handler, const std::string &args);
};
}

#endif /*!__LIBANTICHEAT_HPP_*/