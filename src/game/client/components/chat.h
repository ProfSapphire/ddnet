/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CHAT_H
#define GAME_CLIENT_COMPONENTS_CHAT_H

#include <base/str.h>

#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/shared/ringbuffer.h>

#include <generated/protocol7.h>

#include <game/client/component.h>
#include <game/client/lineinput.h>
#include <game/client/render.h>

#include <memory>
#include <engine/shared/http.h>

#include <vector>

constexpr auto SAVES_FILE = "ddnet-saves.txt";

class CChat : public CComponent
{
	static constexpr float CHAT_HEIGHT_FULL = 200.0f;
	static constexpr float CHAT_HEIGHT_MIN = 50.0f;
	static constexpr float CHAT_FONTSIZE_WIDTH_RATIO = 2.5f;

	enum
	{
		MAX_LINES = 64,
		MAX_LINE_LENGTH = 256
	};

	CLineInputBuffered<MAX_LINE_LENGTH> m_Input;
	class CLine
	{
	public:
		CLine();
		void Reset(CChat &This);

		bool m_Initialized;
		int64_t m_Time;
		float m_aYOffset[2];
		int m_ClientId;
		int m_TeamNumber;
		bool m_Team;
		bool m_Whisper;
		int m_NameColor;
		char m_aName[64];
		char m_aText[MAX_LINE_LENGTH];
		bool m_Friend;
		bool m_Highlighted;
		std::optional<ColorRGBA> m_CustomColor;

		STextContainerIndex m_TextContainerIndex;
		int m_QuadContainerIndex;

		std::shared_ptr<CManagedTeeRenderInfo> m_pManagedTeeRenderInfo;

		float m_TextYOffset;

		int m_TimesRepeated;
	};

	bool m_PrevScoreBoardShowed;
	bool m_PrevShowChat;

	CLine m_aLines[MAX_LINES];
	int m_CurrentLine;

	enum
	{
		// client IDs for special messages
		CLIENT_MSG = -2,
		SERVER_MSG = -1,
	};

	enum
	{
		MODE_NONE = 0,
		MODE_ALL,
		MODE_TEAM,
	};

	enum
	{
		CHAT_SERVER = 0,
		CHAT_HIGHLIGHT,
		CHAT_CLIENT,
		CHAT_NUM,
	};

	int m_Mode;
	bool m_Show;
	bool m_CompletionUsed;
	int m_CompletionChosen;
	char m_aCompletionBuffer[MAX_LINE_LENGTH];
	int m_PlaceholderOffset;
	int m_PlaceholderLength;
	static char ms_aDisplayText[MAX_LINE_LENGTH];
	class CRateablePlayer
	{
	public:
		int m_ClientId;
		int m_Score;
	};
	CRateablePlayer m_aPlayerCompletionList[MAX_CLIENTS];
	int m_PlayerCompletionListLength;

	struct CCommand
	{
		char m_aName[IConsole::TEMPCMD_NAME_LENGTH];
		char m_aParams[IConsole::TEMPCMD_PARAMS_LENGTH];
		char m_aHelpText[IConsole::TEMPCMD_HELP_LENGTH];

		CCommand() = default;
		CCommand(const char *pName, const char *pParams, const char *pHelpText)
		{
			str_copy(m_aName, pName);
			str_copy(m_aParams, pParams);
			str_copy(m_aHelpText, pHelpText);
		}

		bool operator<(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) < 0; }
		bool operator<=(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) <= 0; }
		bool operator==(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) == 0; }
	};

	std::vector<CCommand> m_vServerCommands;
	bool m_ServerCommandsNeedSorting;

	struct CHistoryEntry
	{
		int m_Team;
		char m_aText[1];
	};
	CHistoryEntry *m_pHistoryEntry;
	CStaticRingBuffer<CHistoryEntry, 64 * 1024, CRingBufferBase::FLAG_RECYCLE> m_History;
	int m_PendingChatCounter;
	int64_t m_LastChatSend;
	int64_t m_aLastSoundPlayed[CHAT_NUM];
	bool m_IsInputCensored;
	char m_aCurrentInputText[MAX_LINE_LENGTH];
	bool m_EditingNewLine;

	bool m_ServerSupportsCommandInfo;

	// ----- Ollama AI auto-reply -----
	static constexpr const char *OLLAMA_URL = "http://localhost:11434/api/chat";
	static constexpr int OLLAMA_MAX_HISTORY_TEXT = 512;
	static constexpr int OLLAMA_PENDING_QUEUE_SIZE = 3;
	static constexpr int OLLAMA_PENDING_ECHO_LIMIT = 8;
	static constexpr int OLLAMA_RUNTIME_PROMPT_SIZE = 2048;
	static constexpr int OLLAMA_LANGUAGE_HINT_SIZE = 128;
	static constexpr int OLLAMA_MAP_NAME_SIZE = 128;
	static constexpr int OLLAMA_MOOD_LABEL_SIZE = 16;
	static constexpr int OLLAMA_PLAYER_MEMORY_LIMIT = 16;
	static constexpr int OLLAMA_RECENT_PUBLIC_CHAT_LIMIT = 24;

	enum class EOllamaTriggerKind
	{
		ADDRESSED = 0,
		IDLE,
	};

	struct SOllamaHistoryEntry
	{
		bool m_Assistant = false;
		char m_aText[OLLAMA_MAX_HISTORY_TEXT] = {0};
	};

	struct SOllamaPlayerMemory
	{
		char m_aPlayerName[64] = {0};
		char m_aLastTopicExcerpt[65] = {0};
		char m_aLastPreferenceExcerpt[49] = {0};
		int64_t m_LastSeenTime = 0;
	};

	struct SOllamaRecentPublicChat
	{
		char m_aSender[64] = {0};
		char m_aText[MAX_LINE_LENGTH] = {0};
		int64_t m_Time = 0;
	};

	struct SOllamaPendingRequest
	{
		int64_t m_RequestId = 0;
		EOllamaTriggerKind m_TriggerKind = EOllamaTriggerKind::ADDRESSED;
		char m_aSpeaker[64] = {0};
		char m_aInputText[MAX_LINE_LENGTH] = {0};
		std::vector<SOllamaHistoryEntry> m_vContext;
		char m_aDetectedLanguage[32] = {0};
		char m_aLanguageHint[OLLAMA_LANGUAGE_HINT_SIZE] = {0};
		char m_aMapName[OLLAMA_MAP_NAME_SIZE] = {0};
		char m_aPersonaName[64] = {0};
		int m_MoodScore = 0;
		char m_aMoodLabel[OLLAMA_MOOD_LABEL_SIZE] = {0};
		SOllamaPlayerMemory m_SpeakerMemory;
		char m_aBaseSystemPrompt[512] = {0};
		char m_aRuntimeSystemPrompt[OLLAMA_RUNTIME_PROMPT_SIZE] = {0};
	};

	struct SOllamaPendingEcho
	{
		char m_aText[MAX_LINE_LENGTH] = {0};
	};

	std::shared_ptr<CHttpRequest> m_pOllamaRequest;
	bool m_OllamaRequestPending = false;
	SOllamaPendingRequest m_ActiveOllamaRequest;
	std::vector<SOllamaHistoryEntry> m_vOllamaHistory;
	std::vector<SOllamaPendingRequest> m_vOllamaPendingRequests;
	std::vector<SOllamaPendingEcho> m_vOllamaPendingEchoes;
	std::vector<SOllamaPlayerMemory> m_vOllamaPlayerMemories;
	std::vector<SOllamaRecentPublicChat> m_vRecentPublicChats;
	int64_t m_NextOllamaRequestId = 1;
	int m_OllamaMoodScore = 0;
	int64_t m_LastOllamaReplyTime = 0;
	int64_t m_LastOllamaIdleChatTime = 0;

	void CancelOllamaWork();
	void ResetOllamaState();
	void AddOllamaHistoryEntry(bool Assistant, const char *pText);
	void AddPublicChatToOllamaHistory(const char *pSenderName, const char *pMessage);
	void EnqueueOllamaRequest(EOllamaTriggerKind TriggerKind, const char *pSpeakerName, const char *pMessage);
	void StartNextOllamaRequest();
	bool ConsumePendingOllamaEcho(const char *pText);
	bool IsPublicPlayerChat(int ClientId, int Team) const;
	bool IsLocalClient(int ClientId) const;
	const char *StripLocalNamePrefix(const char *pMessage) const;
	void OnOllamaResponse();
	void FinishOllamaRequest();
	void BuildRuntimeSystemPrompt(SOllamaPendingRequest &Request) const;
	void DetectOllamaLanguageHint(const char *pMessage, char *pLanguageHint, size_t LanguageHintSize, char *pDetectedLanguage, size_t DetectedLanguageSize) const;
	void UpdateOllamaMood(const char *pMessage);
	void UpdateOllamaPlayerMemory(const char *pSpeakerName, const char *pMessage, int64_t Now);
	bool GetOllamaPlayerMemorySnapshot(const char *pSpeakerName, SOllamaPlayerMemory &Memory) const;
	void AddRecentPublicChatEvent(const char *pSpeakerName, const char *pMessage, int64_t Now);
	void PruneRecentPublicChatEvents(int64_t Now);
	bool IsIdleOllamaEligible(int64_t Now) const;
	void MaybeEnqueueIdleOllamaRequest(int64_t Now);
	void AppendOllamaLogEvent(const char *pEventType, const SOllamaPendingRequest &Request, const char *pOutgoingText, int HttpStatus, const char *pErrorReason) const;
	void GetOllamaPersonaName(char *pBuffer, size_t BufferSize) const;
	void BuildOllamaVisibleReply(const SOllamaPendingRequest &Request, const char *pResponseText, char *pBuffer, size_t BufferSize) const;
	const char *GetOllamaMoodLabel(int MoodScore) const;
	// --------------------------------

	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSayTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConChat(IConsole::IResult *pResult, void *pUserData);
	static void ConShowChat(IConsole::IResult *pResult, void *pUserData);
	static void ConEcho(IConsole::IResult *pResult, void *pUserData);
	static void ConClearChat(IConsole::IResult *pResult, void *pUserData);

	static void ConchainChatOld(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatFontSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatWidth(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	bool LineShouldHighlight(const char *pLine, const char *pName);
	void StoreSave(const char *pText);

public:
	CChat();
	int Sizeof() const override { return sizeof(*this); }

	static constexpr float MESSAGE_TEE_PADDING_RIGHT = 0.5f;

	bool IsActive() const { return m_Mode != MODE_NONE; }
	void AddLine(int ClientId, int Team, const char *pLine);
	void EnableMode(int Team);
	void DisableMode();
	void RegisterCommand(const char *pName, const char *pParams, const char *pHelpText);
	void UnregisterCommand(const char *pName);
	void Echo(const char *pString);

	void OnWindowResize() override;
	void OnConsoleInit() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
	void OnPrepareLines(float y);
	void Reset();
	void OnRelease() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void OnInit() override;

	void RebuildChat();
	void ClearLines();

	void EnsureCoherentFontSize() const;
	void EnsureCoherentWidth() const;

	float FontSize() const { return g_Config.m_ClChatFontSize / 10.0f; }
	float MessagePaddingX() const { return FontSize() * (5 / 6.f); }
	float MessagePaddingY() const { return FontSize() * (1 / 6.f); }
	float MessageTeeSize() const { return FontSize() * (7 / 6.f); }
	float MessageRounding() const { return FontSize() * (1 / 2.f); }

	// ----- send functions -----

	// Sends a chat message to the server.
	//
	// @param Team MODE_ALL=0 MODE_TEAM=1
	// @param pLine the chat message
	void SendChat(int Team, const char *pLine);

	// Sends a chat message to the server.
	//
	// It uses a queue with a maximum of 3 entries
	// that ensures there is a minimum delay of one second
	// between sent messages.
	//
	// It uses team or public chat depending on m_Mode.
	void SendChatQueued(const char *pLine);
};
#endif
