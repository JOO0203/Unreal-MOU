#include "Rooms.h"

#include "Framing.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

namespace MOU
{
namespace Rooms
{
namespace
{
	struct Room
	{
		uint32_t    RoomId = 0;
		uint64_t    HostUserId = 0;
		std::string HostName;
		std::string HostAddress;    // 서버가 TCP 피어에서 읽은 값
		uint16_t    HostPort = 0;
		std::string Title;
		bool        bHasPassword = false;
		std::string Password;       // 숫자 4자리. 방과 함께 사라지는 휘발성 값이다
		uint8_t     CurrentPlayers = 1;
		uint8_t     MaxPlayers = static_cast<uint8_t>(kMaxPlayersInRoom);
		ERoomState  State = ERoomState::Waiting;
	};

	std::mutex               GMutex;
	std::map<uint32_t, Room> GRooms;       // RoomId -> Room. 번호순 정렬이 목록 순서로도 쓸 만하다
	uint32_t                 GNextRoomId = 1;

	// 한 사람이 방을 여러 개 만들지 못하게 막는다.
	// 리슨서버는 한 프로세스당 하나뿐이라, 방이 둘이면 하나는 반드시 유령이 된다.
	const Room* FindRoomByHost(uint64_t HostUserId)
	{
		for (const auto& Pair : GRooms)
		{
			if (Pair.second.HostUserId == HostUserId)
			{
				return &Pair.second;
			}
		}
		return nullptr;
	}
}

ERoomResult Create(uint64_t HostUserId, const std::string& HostName,
                   const std::string& HostAddress, uint16_t HostPort,
                   const std::string& Title, bool bHasPassword,
                   const std::string& Password, uint8_t MaxPlayers,
                   uint32_t& OutRoomId)
{
	if (HostPort == 0 || Title.empty())
	{
		return ERoomResult::InvalidRequest;
	}
	if (bHasPassword && Password.size() != kRoomPasswordLen)
	{
		return ERoomResult::InvalidRequest;
	}

	std::lock_guard<std::mutex> Lock(GMutex);

	if (FindRoomByHost(HostUserId) != nullptr)
	{
		return ERoomResult::AlreadyHosting;
	}

	Room NewRoom;
	NewRoom.RoomId       = GNextRoomId++;
	NewRoom.HostUserId   = HostUserId;
	NewRoom.HostName     = HostName;
	NewRoom.HostAddress  = HostAddress;
	NewRoom.HostPort     = HostPort;
	NewRoom.Title        = Title;
	NewRoom.bHasPassword = bHasPassword;
	NewRoom.Password     = bHasPassword ? Password : std::string();
	NewRoom.CurrentPlayers = 1;   // 방장 본인
	NewRoom.MaxPlayers   = (MaxPlayers == 0 || MaxPlayers > kMaxPlayersInRoom)
	                       ? static_cast<uint8_t>(kMaxPlayersInRoom) : MaxPlayers;

	OutRoomId = NewRoom.RoomId;
	GRooms.emplace(NewRoom.RoomId, std::move(NewRoom));
	return ERoomResult::Success;
}

ERoomResult Join(uint32_t RoomId, const std::string& Password,
                 std::string& OutHostAddress, uint16_t& OutHostPort)
{
	std::lock_guard<std::mutex> Lock(GMutex);

	auto It = GRooms.find(RoomId);
	if (It == GRooms.end())
	{
		return ERoomResult::NotFound;
	}

	const Room& R = It->second;

	if (R.State != ERoomState::Waiting)
	{
		return ERoomResult::AlreadyStarted;
	}
	if (R.CurrentPlayers >= R.MaxPlayers)
	{
		return ERoomResult::Full;
	}
	if (R.bHasPassword && Password != R.Password)
	{
		return ERoomResult::WrongPassword;
	}

	// 여기까지 왔을 때만 주소를 알려준다.
	OutHostAddress = R.HostAddress;
	OutHostPort    = R.HostPort;

	// 인원수는 여기서 올리지 않는다.
	// 실제로 접속에 성공했는지는 호스트만 알 수 있고,
	// 호스트가 RoomStateUpdate 로 알려주는 값이 진실이다.
	// 여기서 낙관적으로 올리면 접속에 실패한 사람 때문에 방이 영영 꽉 차 보인다.
	return ERoomResult::Success;
}

void ListWaiting(std::vector<RoomInfo>& Out, size_t MaxCount)
{
	std::lock_guard<std::mutex> Lock(GMutex);

	Out.clear();

	// 최근에 만든 방이 위로 오도록 뒤에서부터 훑는다(RoomId 는 증가하므로).
	for (auto It = GRooms.rbegin(); It != GRooms.rend() && Out.size() < MaxCount; ++It)
	{
		const Room& R = It->second;

		// 시작됐거나 꽉 찬 방은 들어갈 수 없으므로 목록에서 뺀다.
		if (R.State != ERoomState::Waiting || R.CurrentPlayers >= R.MaxPlayers)
		{
			continue;
		}

		RoomInfo Info{};
		Info.RoomId         = R.RoomId;
		Info.HostUserId     = R.HostUserId;
		Info.CurrentPlayers = R.CurrentPlayers;
		Info.MaxPlayers     = R.MaxPlayers;
		Info.bHasPassword   = R.bHasPassword ? 1 : 0;
		Info.State          = static_cast<uint8_t>(R.State);
		CopyFixedString(Info.Title,    kMaxRoomTitleLen, R.Title);
		CopyFixedString(Info.HostName, kMaxNameLen,      R.HostName);

		Out.push_back(Info);
	}
}

ERoomResult UpdateState(uint32_t RoomId, uint64_t RequesterUserId,
                        uint8_t CurrentPlayers, ERoomState State)
{
	std::lock_guard<std::mutex> Lock(GMutex);

	auto It = GRooms.find(RoomId);
	if (It == GRooms.end())
	{
		return ERoomResult::NotFound;
	}

	Room& R = It->second;

	// 방장만 고칠 수 있다. 남이 남의 방을 "시작됨" 으로 만들어 목록에서 지우는 것을 막는다.
	if (R.HostUserId != RequesterUserId)
	{
		return ERoomResult::NotAuthed;
	}

	R.CurrentPlayers = (CurrentPlayers > R.MaxPlayers) ? R.MaxPlayers : CurrentPlayers;
	R.State          = State;
	return ERoomResult::Success;
}

void RemoveByHost(uint64_t HostUserId)
{
	std::lock_guard<std::mutex> Lock(GMutex);

	for (auto It = GRooms.begin(); It != GRooms.end(); ++It)
	{
		if (It->second.HostUserId == HostUserId)
		{
			GRooms.erase(It);
			return;   // 한 사람당 방은 하나뿐이다
		}
	}
}

size_t Count()
{
	std::lock_guard<std::mutex> Lock(GMutex);
	return GRooms.size();
}

} // namespace Rooms
} // namespace MOU
