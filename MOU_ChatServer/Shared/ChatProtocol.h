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
	constexpr uint16_t kProtocolVersion = 2;

	// BodySize 가 이 값을 넘으면 악성 패킷으로 보고 연결을 끊는다.
	constexpr uint32_t kMaxBodySize = 4096;
	constexpr uint32_t kMaxNameLen  = 32;
	constexpr uint32_t kMaxTextLen  = 512;

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
	enum class ELoginResult : uint8_t
	{
		Success         = 0,
		VersionMismatch = 1,   // 클라와 서버의 kProtocolVersion 이 다르다. 재접속해도 계속 실패한다
		InvalidRequest  = 2,   // 바디 크기가 맞지 않는다
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

	struct LoginReqBody
	{
		// Version 은 반드시 첫 필드여야 한다.
		// 구조체 전체 크기가 서로 달라도 서버가 이 2바이트만은 읽을 수 있어야
		// "버전이 안 맞다"고 정확히 알려줄 수 있기 때문이다.
		// 앞으로 필드를 추가할 때는 반드시 뒤에 붙이고 Version 은 그대로 둔다.
		uint16_t Version;
		char     Name[kMaxNameLen];
		int32_t  TeamId;
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

#pragma pack(pop)

	// 패딩이 끼면 서버와 클라이언트의 해석이 어긋난다.
	// #pragma pack(1) 이 빠지거나 필드 순서를 바꿨을 때 여기서 잡힌다.
	static_assert(sizeof(PacketHeader)      ==  6, "PacketHeader 는 6바이트여야 한다");
	static_assert(sizeof(LoginReqBody)      == 38, "LoginReqBody 에 패딩이 끼었다");
	static_assert(sizeof(LoginAckBody)      == 48, "LoginAckBody 에 패딩이 끼었다");
	static_assert(sizeof(ChatSendBody)      == 11, "ChatSendBody 에 패딩이 끼었다");
	static_assert(sizeof(ChatBroadcastBody) == 51, "ChatBroadcastBody 에 패딩이 끼었다");
	static_assert(sizeof(SetDeadBody)       ==  9, "SetDeadBody 에 패딩이 끼었다");
}
