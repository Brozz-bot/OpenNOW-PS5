#pragma once
#include "../cloud.hpp"
extern "C" {
#include "../platform/ps5-pad.h"
}
#include "media.hpp"
#include "WebSocketClient.hpp"
#include "sdp.hpp"
#include "nvst_qos.hpp"
#include "video_capture.hpp"
#include "input_wire.hpp"
#include "server_control.hpp"
#include "../input/gamepad.hpp"
#include "../input/rumble.hpp"
#include "../input/input_queue.hpp"
extern "C" {
#include "peer_connection.h"
}
struct cJSON;
namespace opennow {
class Stream {
public:
 explicit Stream(Media& m):media_(m){}
 ~Stream(){stop();}
 bool start(const Session&,const char* device);
 void tick(std::uint64_t now);
 void input(const PS5_PadData&,std::uint64_t now);
 void events(InputQueue&,std::uint64_t now);
 void stop();
 bool active() const {return pc_||ws_||runtimeReady_;}
 bool failed() const {return failed_;}
 bool inputReady() const {return inputReady_&&pc_;}
 const char* status() const {return status_;}
 const SessionTimer& sessionTimer() const {return sessionTimer_;}
 const ServerTermination& serverTermination() const {return serverTermination_;}
 bool takeRumble(Rumble&);
private:
 void fail(const char*);void release();
 bool sendInput(const wire::Bytes&,bool partial=false,bool meaningful=true);bool releaseKey(std::uint64_t now);
 std::uint64_t inputTime(std::uint64_t now) const {return now>=handshakeAt_?now-handshakeAt_:0;}
 void openChannels();void updateInputReady();
 void remoteCandidate(std::string);void checkVideoProgress(std::uint64_t now);
 void message(const std::string&);void payload(cJSON*);void send(cJSON*);
 void sendCandidates();
 void recoverVideoLoss();
 void peerInfo();void data(const char*,std::size_t,std::uint16_t);
 static void ice(char*,void*);static void state(PeerConnectionState,void*);
 static void video(const PeerVideoPacket*,void*);static void audio(const PeerAudioPacket*,void*);
 static void dataMessage(char*,std::size_t,void*,std::uint16_t);
 static void dataOpen(void*);static void dataClose(void*);
 Media& media_;PeerConnection* pc_=nullptr;WebSocketClient* ws_=nullptr;
 StreamSettings settings_{};Session session_{};char status_[192]{};std::string name_;int peerId_=0,remoteId_=1,ack_=0,protocol_=2;
 bool failed_=false,runtimeReady_=false,answerSent_=false,inputReady_=false;std::vector<std::string> candidates_,remoteCandidates_;
 std::string candidateMid_;int candidateMLine_=0;
 std::uint64_t nextHeartbeat_=0,started_=0,lastInput_=0,nextChannelAttempt_=0;
 unsigned channelRequests_=0;
 bool handshakeReceived_=false;
 std::uint64_t handshakeAt_=0;
 std::uint32_t decodedFrames_=0,presentedFrames_=0;
 std::uint64_t lastDecodedAt_=0,recoveryRequestedAt_=0;
 bool videoSeen_=false;
 SessionTimer sessionTimer_;
 ServerTermination serverTermination_;
 std::optional<Rumble> rumble_;
 GamepadState previousInput_;
 bool inputSeen_=false;
 unsigned lastVideoLoss_=0;
 std::uint64_t keyframeRequested_=0;
 webrtc::QosFeedback qos_;
 std::uint64_t nextQos_=0;
 video::ShortCapture capture_;
 KeyStroke heldKey_{};
 bool keyHeld_=false;
 std::uint8_t mouseHeld_=0;
 std::uint64_t keyUpAt_=0,nextKeyAt_=0;
 std::uint8_t heldModifiers_=0;
 InputEvent pendingInput_{};
 bool pendingInputValid_=false,releasingInputs_=false,releasingKey_=false,cursorWanted_=false;
 std::uint64_t nextCursorCapture_=0;
};
}
