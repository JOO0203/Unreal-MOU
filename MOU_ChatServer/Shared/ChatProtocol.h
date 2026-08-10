// MOU 채팅 서버 <-> 클라이언트 공용 프로토콜 정의.
//
// 이 파일은 언리얼 클라이언트에서도 그대로 include 하므로
// STL 이나 플랫폼 헤더에 의존하지 않는다. <cstdint> 만 쓴다.
#pragma once

#include <cstdint>

namespace MOU
{
	// 헤더 구조나 오피코드 의미가 바뀌면 올린다.
	// 로그인 시점에 서버가 이 값을 검사하고, 다르면 명확한 사유와 함께 거부한다.
	// 이게 없으면 서버만 업데이트했을 때 클라이언트가 원인 모를 재접속을 무한 반복한다.
	//
	//   1 -> 2 : LoginReqBody 에 Version, LoginAckBody 에 Result/ServerVersion 추가
	//   2 -> 3 : 계정 시스템 도입. LoginReq 가 이름 대신 아이디/비밀번호를 보낸다.
	//            UserId 가 "접속 일련번호" 에서 "계정 고유번호" 로 바뀌었다.
	//   3 -> 4 : 로비(방 목록) 추가. 방 생성/조회/참여 옵코드가 붙었다.
	constexpr uint16_t kProtocolVersion = 4;

	// BodySize 가 이 값을 넘으면 악성 패킷으로 보고 연결을 끊는다.
	constexpr uint32_t kMaxBodySize = 4096;
	constexpr uint32_t kMaxNameLen  = 32;   // 닉네임 (화면에 보이는 이름)
	constexpr uint32_t kMaxTextLen  = 512;

	// 로그인 아이디와 비밀번호의 최대 바이트 수 (UTF-8 기준, 널 종료 포함).
	constexpr uint32_t kMaxLoginIdLen  = 24;
	constexpr uint32_t kMaxPasswordLen = 64;

	// 계정 정책. 서버가 검사하고, 클라이언트는 미리 걸러서 왕복을 아낀다.
	constexpr uint32_t kMinLoginIdLen  = 3;
	constexpr uint32_t kMinPasswordLen = 6;

	// --- 로비 ---
	constexpr uint32_t kMaxRoomTitleLen  = 48;
	constexpr uint32_t kRoomPasswordLen  = 4;    // 숫자 4자리. 널 종료를 두지 않는다
	constexpr uint32_t kMaxPlayersInRoom = 4;    // 1~4인 게임
	constexpr uint32_t kMaxRoomsInList   = 20;   // 한 번에 내려주는 방 개수 상한
	constexpr uint32_t kMaxAddressLen    = 16;   // "255.255.255.255" + 널

	enum class EOpcode : uint16_t
	{
		None          = 0,
		LoginReq      = 1,
		LoginAck      = 2,
		ChatSend      = 3,
		ChatBroadcast = 4,
		HistoryReq    = 5,   // 6단계에서 사용
		HistoryAck    = 6,   // 6단계에서 사용
		WhisperSend   = 7,   // 9단계에서 사용
		SetDead       = 8,   // 지금은 테스트 클라가 보내지만, 나중에는 리슨서버만 보낸다
		Heartbeat     = 9,
		RegisterReq   = 10,  // 계정 생성
		RegisterAck   = 11,

		// --- 로비 (v4) ---
		// 서버는 "방 주소록" 역할만 한다. 실제 게임 트래픽은 여기를 거치지 않고
		// 참가자가 호스트의 리슨서버에 직접 붙는다.
		RoomCreateReq   = 12,
		RoomCreateAck   = 13,
		RoomListReq     = 14,
		RoomListAck     = 15,
		RoomJoinReq     = 16,
		RoomJoinAck     = 17,
		RoomLeaveReq    = 18,  // 호스트가 보내면 방이 사라진다
		RoomStateUpdate = 19,  // 호스트가 인원수/시작여부를 갱신한다
	};

	/** 방 관련 요청의 결과. */
	enum class ERoomResult : uint8_t
	{
		Success        = 0,
		NotAuthed      = 1,   // 로그인하지 않았다
		NotFound       = 2,   // 그런 방이 없다 (이미 닫혔을 수 있다)
		WrongPassword  = 3,
		Full           = 4,
		AlreadyStarted = 5,   // 이미 게임이 시작된 방
		AlreadyHosting = 6,   // 이미 방을 하나 갖고 있다
		InvalidRequest = 7,
	};

	/** 방의 진행 상태. */
	enum class ERoomState : uint8_t
	{
		Waiting = 0,   // 대기 중. 목록에 노출된다
		InGame  = 1,   // 게임 시작됨. 목록에서 감춘다
	};

	enum class EChatChannel : uint8_t
	{
		All     = 0,
		Team    = 1,
		Dead    = 2,
		Whisper = 3,
		System  = 4,
	};

	// 로그인 거부 사유. LoginAckBody::Result 에 담겨 돌아온다.
	// 클라이언트는 이 값을 보고 "재시도해도 소용없는 실패"인지 판단할 수 있다.
	// RegisterAckBody::Result 에도 같은 enum 을 쓴다.
	enum class ELoginResult : uint8_t
	{
		Success         = 0,
		VersionMismatch = 1,   // 클라와 서버의 kProtocolVersion 이 다르다. 재접속해도 계속 실패한다
		InvalidRequest  = 2,   // 바디 크기가 맞지 않는다

		// --- 계정 관련 (v3) ---
		AccountNotFound = 3,   // 그런 아이디가 없다
		WrongPassword   = 4,   // 비밀번호가 틀렸다
		DuplicateId     = 5,   // 가입하려는 아이디가 이미 있다
		InvalidFormat   = 6,   // 아이디/비번 길이 규칙 위반
		ServerError     = 7,   // DB 오류 등 서버 문제. 클라이언트 잘못이 아니다
	};

#pragma pack(push, 1)

	// 모든 패킷 앞에 붙는 고정 헤더.
	// BodySize 는 이 헤더를 제외한 페이로드 크기다.
	// TCP 가 메시지 경계를 보장하지 않으므로, 수신측은 이 길이로 직접 잘라야 한다.
	struct PacketHeader
	{
		uint32_t BodySize;
		uint16_t Opcode;
	};

	// [경고] Password 는 지금 평문으로 전송된다.
	//   서버가 저장할 때는 솔트 + PBKDF2 로 해시하지만, 전송 구간에는 암호화가 없다.
	//   같은 네트워크에 있는 사람이 패킷을 뜨면 비밀번호가 그대로 보인다.
	//   >> 실제로 쓰는 비밀번호를 여기에 쓰지 말 것. 팀에도 공지할 것. <<
	//   제대로 하려면 TLS 를 씌워야 하는데, 그건 이 프로젝트 범위를 넘는다.
	struct LoginReqBody
	{
		// Version 은 반드시 첫 필드여야 한다.
		// 구조체 전체 크기가 서로 달라도 서버가 이 2바이트만은 읽을 수 있어야
		// "버전이 안 맞다"고 정확히 알려줄 수 있기 때문이다.
		// 앞으로 필드를 추가할 때는 반드시 뒤에 붙이고 Version 은 그대로 둔다.
		uint16_t Version;
		char     LoginId[kMaxLoginIdLen];
		char     Password[kMaxPasswordLen];
		int32_t  TeamId;                      // 게임 쪽이 정한다. 계정에 저장하지 않는다
	};

	// 계정 생성. 로그인과 같은 이유로 Version 이 첫 필드다.
	struct RegisterReqBody
	{
		uint16_t Version;
		char     LoginId[kMaxLoginIdLen];
		char     Password[kMaxPasswordLen];
		char     Nickname[kMaxNameLen];       // 화면에 보일 이름
	};

	struct RegisterAckBody
	{
		uint8_t  bSuccess;
		uint8_t  Result;                      // ELoginResult
		uint16_t ServerVersion;
	};

	struct LoginAckBody
	{
		uint64_t UserId;                 // 서버가 부여한다. 0 이면 실패
		int32_t  TeamId;
		char     Name[kMaxNameLen];      // 서버가 확정한 이름
		uint8_t  bSuccess;
		uint8_t  Result;                 // ELoginResult. 실패 사유
		uint16_t ServerVersion;          // 버전 불일치 시 어느 쪽이 낡았는지 바로 알 수 있게
	};

	// 뒤에 TextLen 바이트의 UTF-8 본문이 이어진다.
	struct ChatSendBody
	{
		uint64_t TargetUserId;           // Whisper 전용. 그 외에는 0
		uint16_t TextLen;
		uint8_t  Channel;
	};

	// 뒤에 TextLen 바이트의 UTF-8 본문이 이어진다.
	// SenderUserId / SenderName 은 서버가 세션 정보로 채운다.
	// 클라이언트가 보낸 값을 그대로 옮기지 않는다.
	struct ChatBroadcastBody
	{
		uint64_t SenderUserId;
		int64_t  Timestamp;              // Unix epoch (초)
		char     SenderName[kMaxNameLen];
		uint16_t TextLen;
		uint8_t  Channel;
	};

	struct SetDeadBody
	{
		uint64_t UserId;
		uint8_t  bDead;
	};

	// ------------------------------------------------------------------
	// 로비 (v4)
	// ------------------------------------------------------------------

	// [호스트 주소를 클라이언트가 보내지 않는 이유]
	//   IP 는 서버가 TCP 연결의 상대 주소에서 직접 읽는다.
	//   클라이언트가 "내 IP 는 여기다" 라고 적어 보내게 하면 남의 주소를 적어
	//   엉뚱한 사람에게 접속을 몰아주는 장난이 가능하다.
	//   이름을 서버가 채우는 것과 같은 원칙이다.
	//   포트는 리슨서버가 실제로 여는 값이라 클라이언트만 알 수 있어 받는다.
	struct RoomCreateReqBody
	{
		char     Title[kMaxRoomTitleLen];
		char     Password[kRoomPasswordLen];   // bHasPassword 가 0 이면 무시한다
		uint16_t HostPort;                     // 리슨서버 포트 (보통 7777)
		uint8_t  bHasPassword;
		uint8_t  MaxPlayers;                   // 1~kMaxPlayersInRoom
	};

	struct RoomCreateAckBody
	{
		uint32_t RoomId;                       // 0 이면 실패
		uint8_t  bSuccess;
		uint8_t  Result;                       // ERoomResult
	};

	// RoomListAckBody 뒤에 Count 개의 RoomInfo 가 이어붙는다.
	//
	// 주의: 여기에는 호스트 주소가 없다.
	//   목록만 보고 바로 접속하지 못하게 일부러 뺐다.
	//   주소는 RoomJoinReq 로 비밀번호 검사를 통과해야 받을 수 있다.
	struct RoomInfo
	{
		uint32_t RoomId;
		uint64_t HostUserId;
		char     Title[kMaxRoomTitleLen];
		char     HostName[kMaxNameLen];
		uint8_t  CurrentPlayers;
		uint8_t  MaxPlayers;
		uint8_t  bHasPassword;
		uint8_t  State;                        // ERoomState
	};

	struct RoomListAckBody
	{
		uint16_t Count;
	};

	struct RoomJoinReqBody
	{
		uint32_t RoomId;
		char     Password[kRoomPasswordLen];   // 비밀번호 방이 아니면 무시된다
	};

	struct RoomJoinAckBody
	{
		uint32_t RoomId;
		char     HostAddress[kMaxAddressLen];  // 성공했을 때만 채워진다
		uint16_t HostPort;
		uint8_t  bSuccess;
		uint8_t  Result;                       // ERoomResult
	};

	// 호스트가 인원 변화나 게임 시작을 알린다. 방장만 보낼 수 있다.
	struct RoomStateUpdateBody
	{
		uint32_t RoomId;
		uint8_t  CurrentPlayers;
		uint8_t  State;                        // ERoomState
	};

#pragma pack(pop)

	// 패딩이 끼면 서버와 클라이언트의 해석이 어긋난다.
	// #pragma pack(1) 이 빠지거나 필드 순서를 바꿨을 때 여기서 잡힌다.
	static_assert(sizeof(PacketHeader)      ==  6, "PacketHeader 는 6바이트여야 한다");
	static_assert(sizeof(LoginReqBody)      == 94, "LoginReqBody 에 패딩이 끼었다");
	static_assert(sizeof(RegisterReqBody)   == 122, "RegisterReqBody 에 패딩이 끼었다");
	static_assert(sizeof(RegisterAckBody)   ==  4, "RegisterAckBody 에 패딩이 끼었다");
	static_assert(sizeof(LoginAckBody)      == 48, "LoginAckBody 에 패딩이 끼었다");
	static_assert(sizeof(ChatSendBody)      == 11, "ChatSendBody 에 패딩이 끼었다");
	static_assert(sizeof(ChatBroadcastBody) == 51, "ChatBroadcastBody 에 패딩이 끼었다");
	static_assert(sizeof(SetDeadBody)       ==  9, "SetDeadBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomCreateReqBody) == 56, "RoomCreateReqBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomCreateAckBody) ==  6, "RoomCreateAckBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomInfo)          == 96, "RoomInfo 에 패딩이 끼었다");
	static_assert(sizeof(RoomListAckBody)   ==  2, "RoomListAckBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomJoinReqBody)   ==  8, "RoomJoinReqBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomJoinAckBody)   == 24, "RoomJoinAckBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomStateUpdateBody) == 6, "RoomStateUpdateBody 에 패딩이 끼었다");

	// 방 목록 한 번에 담을 수 있는지 확인한다. 넘치면 kMaxRoomsInList 를 줄여야 한다.
	static_assert(sizeof(RoomListAckBody) + sizeof(RoomInfo) * kMaxRoomsInList <= kMaxBodySize,
	              "방 목록이 kMaxBodySize 를 넘는다");
}
