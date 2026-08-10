// MOU 로비 - 방 목록 레지스트리.
//
// [이 서버가 하는 일과 하지 않는 일]
//   한다:   "지금 어떤 방이 열려 있고, 호스트 주소가 무엇인지" 를 보관하고 알려준다.
//   안 한다: 게임 시뮬레이션. 그건 방장의 리슨서버가 전부 처리한다.
//
//   즉 이 서버는 주소록이다. 참가자가 방 주소를 받아 호스트에게 직접 붙고 나면
//   게임 트래픽은 이 프로세스를 한 바이트도 지나가지 않는다.
//   그래서 방이 몇 개 열려도 서버 부하는 사실상 늘지 않는다.
//
// [왜 SQLite 에 저장하지 않는가]
//   방은 휘발성이다. 서버가 재시작하면 호스트들의 리슨서버도 이미 죽어 있으므로,
//   방 목록을 복원하는 것은 오히려 "들어갈 수 없는 방" 을 보여주는 셈이 된다.
//   계정(영속)과 채팅 로그(영속)와 달리 방은 메모리에만 둔다.
//
// [수명]
//   방은 호스트의 TCP 연결에 묶인다. 호스트가 접속을 끊으면(게임 종료, 강제 종료,
//   랜선 뽑힘 모두 포함) 세션 정리 과정에서 방도 같이 사라진다.
//   그래서 "유령 방" 이 목록에 남지 않는다.

#pragma once

#include "ChatProtocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MOU
{
	namespace Rooms
	{
		/**
		 * 방을 만든다.
		 *
		 * @param HostUserId  방장의 계정 번호
		 * @param HostName    방장 닉네임 (목록 표시용)
		 * @param HostAddress 서버가 TCP 연결에서 읽은 방장의 IP. 클라이언트 신고값이 아니다
		 * @param OutRoomId   성공 시 새 방 번호
		 */
		ERoomResult Create(uint64_t HostUserId, const std::string& HostName,
		                   const std::string& HostAddress, uint16_t HostPort,
		                   const std::string& Title, bool bHasPassword,
		                   const std::string& Password, uint8_t MaxPlayers,
		                   uint32_t& OutRoomId);

		/**
		 * 참여를 시도한다. 성공해야만 호스트 주소를 돌려준다.
		 *
		 * 비밀번호를 여기서 검사하는 이유는 UX 다 — 틀린 비밀번호로 굳이 접속을
		 * 시도했다가 튕기는 것보다, 주소를 아예 주지 않는 편이 낫다.
		 *
		 * >> 다만 이것이 유일한 관문이어서는 안 된다. <<
		 *    목록을 거치지 않고 호스트 IP 로 직접 붙는 것은 막을 수 없으므로,
		 *    호스트의 GameMode::PreLogin 에서도 반드시 다시 검사해야 한다.
		 */
		ERoomResult Join(uint32_t RoomId, const std::string& Password,
		                 std::string& OutHostAddress, uint16_t& OutHostPort);

		/** 대기 중인 방들을 최신순으로 담아준다. 꽉 찬 방과 시작된 방은 빼고 준다. */
		void ListWaiting(std::vector<RoomInfo>& Out, size_t MaxCount);

		/**
		 * 호스트가 인원수나 진행 상태를 갱신한다.
		 * 방장이 아닌 사람이 부르면 아무 일도 일어나지 않는다.
		 */
		ERoomResult UpdateState(uint32_t RoomId, uint64_t RequesterUserId,
		                        uint8_t CurrentPlayers, ERoomState State);

		/**
		 * 해당 유저가 방장인 방을 없앤다.
		 * 접속이 끊길 때 무조건 불러서 유령 방이 남지 않게 한다.
		 * 방장이 아니면 아무 일도 하지 않으므로 그냥 불러도 안전하다.
		 */
		void RemoveByHost(uint64_t HostUserId);

		/** 지금 열려 있는 방 개수. 로그용. */
		size_t Count();
	}
}
