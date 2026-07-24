# Graph Report - .  (2026-07-24)

## Corpus Check
- Large corpus: 3863 files · ~802,296 words. Semantic extraction will be expensive (many Claude tokens). Consider running on a subfolder.

## Summary
- 1174 nodes · 1519 edges · 90 communities (82 shown, 8 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 16 edges (avg confidence: 0.81)
- Token cost: 88,608 input · 3,616 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Chat Photo Provider|Chat Photo Provider]]
- [[_COMMUNITY_Utils & Formatting|Utils & Formatting]]
- [[_COMMUNITY_App Manager|App Manager]]
- [[_COMMUNITY_Chat Entity|Chat Entity]]
- [[_COMMUNITY_Message Entity|Message Entity]]
- [[_COMMUNITY_Message Model|Message Model]]
- [[_COMMUNITY_Storage Manager|Storage Manager]]
- [[_COMMUNITY_User Entity|User Entity]]
- [[_COMMUNITY_Chat Manager|Chat Manager]]
- [[_COMMUNITY_Authorization Flow|Authorization Flow]]
- [[_COMMUNITY_Chat Model|Chat Model]]
- [[_COMMUNITY_Lottie Animation|Lottie Animation]]
- [[_COMMUNITY_Localization|Localization]]
- [[_COMMUNITY_Message Service Content|Message Service Content]]
- [[_COMMUNITY_Settings|Settings]]
- [[_COMMUNITY_Chat Info Formatter|Chat Info Formatter]]
- [[_COMMUNITY_Country Model|Country Model]]
- [[_COMMUNITY_Chat Properties|Chat Properties]]
- [[_COMMUNITY_Chat Status Formatting|Chat Status Formatting]]
- [[_COMMUNITY_Chat Folder Info|Chat Folder Info]]
- [[_COMMUNITY_File Download|File Download]]
- [[_COMMUNITY_Supergroup Entity|Supergroup Entity]]
- [[_COMMUNITY_Chat List & Position|Chat List & Position]]
- [[_COMMUNITY_Authorization Actions|Authorization Actions]]
- [[_COMMUNITY_QR Code Generation|QR Code Generation]]
- [[_COMMUNITY_ARGS|ARGS]]
- [[_COMMUNITY_backFetching|backFetching]]
- [[_COMMUNITY_basicGroup|basicGroup]]
- [[_COMMUNITY_Authorization|Authorization]]
- [[_COMMUNITY_QObject|QObject]]
- [[_COMMUNITY_canFetchMore|canFetchMore]]
- [[_COMMUNITY_QrCodeItem|QrCodeItem]]
- [[_COMMUNITY_componentComplete|componentComplete]]
- [[_COMMUNITY_BasicGroup|BasicGroup]]
- [[_COMMUNITY_Emoji|Emoji]]
- [[_COMMUNITY_callingCode|callingCode]]
- [[_COMMUNITY_QAbstractListModel|QAbstractListModel]]
- [[_COMMUNITY_Q_PROPERTY|Q_PROPERTY]]
- [[_COMMUNITY_AppManager|AppManager]]
- [[_COMMUNITY_Q_PROPERTY|Q_PROPERTY]]
- [[_COMMUNITY_Chat|Chat]]
- [[_COMMUNITY_BasicGroup|BasicGroup]]
- [[_COMMUNITY_Chat|Chat]]
- [[_COMMUNITY_PluralRules_French|PluralRules_French]]
- [[_COMMUNITY_PluralRules|PluralRules]]
- [[_COMMUNITY_Q_PROPERTY|Q_PROPERTY]]
- [[_COMMUNITY_Q_PROPERTY|Q_PROPERTY]]
- [[_COMMUNITY_handleDeleteMessages|handleDeleteMessages]]
- [[_COMMUNITY_Q_PROPERTY|Q_PROPERTY]]
- [[_COMMUNITY_Q_PROPERTY|Q_PROPERTY]]
- [[_COMMUNITY_Q_PROPERTY|Q_PROPERTY]]
- [[_COMMUNITY_Q_PROPERTY|Q_PROPERTY]]
- [[_COMMUNITY_Q_PROPERTY|Q_PROPERTY]]
- [[_COMMUNITY_Q_PROPERTY|Q_PROPERTY]]
- [[_COMMUNITY_Q_PROPERTY|Q_PROPERTY]]
- [[_COMMUNITY_MeeGram Project|MeeGram Project]]
- [[_COMMUNITY_handleAuthorizationState|handleAuthorizationState]]
- [[_COMMUNITY_Locale|Locale]]
- [[_COMMUNITY_PluralRules_Arabic|PluralRules_Arabic]]
- [[_COMMUNITY_PluralRules_Balkan|PluralRules_Balkan]]
- [[_COMMUNITY_PluralRules_Breton|PluralRules_Breton]]
- [[_COMMUNITY_PluralRules_Czech|PluralRules_Czech]]
- [[_COMMUNITY_PluralRules_Langi|PluralRules_Langi]]
- [[_COMMUNITY_PluralRules_Latvian|PluralRules_Latvian]]
- [[_COMMUNITY_PluralRules_Lithuanian|PluralRules_Lithuanian]]
- [[_COMMUNITY_PluralRules_Macedonian|PluralRules_Macedonian]]
- [[_COMMUNITY_PluralRules_Maltese|PluralRules_Maltese]]
- [[_COMMUNITY_PluralRules_None|PluralRules_None]]
- [[_COMMUNITY_PluralRules_One|PluralRules_One]]
- [[_COMMUNITY_PluralRules_Polish|PluralRules_Polish]]
- [[_COMMUNITY_PluralRules_Serbian|PluralRules_Serbian]]
- [[_COMMUNITY_PluralRules_Slovenian|PluralRules_Slovenian]]
- [[_COMMUNITY_PluralRules_Tachelhit|PluralRules_Tachelhit]]
- [[_COMMUNITY_PluralRules_Two|PluralRules_Two]]
- [[_COMMUNITY_PluralRules_Welsh|PluralRules_Welsh]]
- [[_COMMUNITY_PluralRules_Zero|PluralRules_Zero]]
- [[_COMMUNITY_QDeclarativeItem|QDeclarativeItem]]
- [[_COMMUNITY_PluralRules_Zero|PluralRules_Zero]]
- [[_COMMUNITY_Settings|Settings]]
- [[_COMMUNITY_Client|Client]]
- [[_COMMUNITY_MeeGram Documentation|MeeGram Documentation]]
- [[_COMMUNITY_PluralRules_Welsh|PluralRules_Welsh]]
- [[_COMMUNITY_PluralRules_Arabic|PluralRules_Arabic]]

## God Nodes (most connected - your core abstractions)
1. `Chat` - 46 edges
2. `MessageModel` - 46 edges
3. `AppManager` - 45 edges
4. `StorageManager()` - 44 edges
5. `ChatModel()` - 37 edges
6. `Authorization` - 34 edges
7. `LottieAnimation` - 33 edges
8. `ChatManager` - 33 edges
9. `CountryModel()` - 31 edges
10. `Message` - 29 edges

## Surprising Connections (you probably didn't know these)
- `LottieAnimation` --references--> `MeeGram Project`  [INFERRED]
  src/LottieAnimation.cpp → CMakeLists.txt
- `QrCodeItem` --references--> `MeeGram Project`  [INFERRED]
  src/QrCodeItem.cpp → CMakeLists.txt
- `UIConstants` --conceptually_related_to--> `MeeGram Documentation`  [INFERRED]
  resources/qml/components/UIConstants.js → README.md
- `Authorization` --calls--> `result`  [INFERRED]
  src/Authorization.hpp → src/Client.hpp
- `AppManager` --calls--> `close()`  [EXTRACTED]
  src/AppManager.hpp → src/AppManager.cpp

## Communities (90 total, 8 thin omitted)

### Community 0 - "Chat Photo Provider"
Cohesion: 0.05
Nodes (30): QDeclarativeImageProvider, ChatPhotoProvider, requestImage, Client, clientId, initialize, m_clientId, m_clientManager (+22 more)

### Community 1 - "Utils & Formatting"
Cohesion: 0.06
Nodes (37): Chat, getAudioTitle(), getCallContent(), getChatTitle(), getContent(), getSenderAuthor(), getSenderName(), getServiceContent() (+29 more)

### Community 2 - "App Manager"
Cohesion: 0.05
Nodes (40): AppManager, appInitialized, authorization, authorizationChanged, authorizedChanged, chatManager, chatManagerChanged, checkInitializationStatus (+32 more)

### Community 3 - "Chat Entity"
Cohesion: 0.05
Nodes (40): Chat, id, isMarkedAsUnread, isMuted, lastMessage, lastReadInboxMessageId, lastReadOutboxMessageId, m_chat (+32 more)

### Community 4 - "Message Entity"
Cohesion: 0.05
Nodes (27): Message, chatId, content, contentType, contentTypeString, date, editDate, id (+19 more)

### Community 5 - "Message Model"
Cohesion: 0.05
Nodes (39): chatChanged, MessageModel, backFetching, backFetchingChanged, canFetchMore, count, countChanged, data (+31 more)

### Community 6 - "Storage Manager"
Cohesion: 0.06
Nodes (33): StorageManager(), basicGroup, basicGroupFullInfoUpdated, chat, chatFolders, chatIds, client, file (+25 more)

### Community 7 - "User Entity"
Cohesion: 0.06
Nodes (22): setStatus(), User(), activeUsernames, determineType, firstName, id, isSupport, lastName (+14 more)

### Community 8 - "Chat Manager"
Cohesion: 0.07
Nodes (31): ChatManager, archivedModel, archivedModelChanged, chatInfoFormatter, closeChat, folderModel, folderModelChanged, folderModels (+23 more)

### Community 9 - "Authorization Flow"
Cohesion: 0.07
Nodes (30): Authorization, checkCode, checkPassword, content, contentChanged, countryModel, deleteAccount, _destroy (+22 more)

### Community 10 - "Chat Model"
Cohesion: 0.07
Nodes (30): ChatModel(), canFetchMore, clear, count, countChanged, data, fetchMore, getChatPosition (+22 more)

### Community 11 - "Lottie Animation"
Cohesion: 0.07
Nodes (30): LottieAnimation, componentComplete, finished, initializeAnimation, loadContent, loop, loopChanged, m_animation (+22 more)

### Community 12 - "Localization"
Cohesion: 0.09
Nodes (26): QTranslator, addRules(), formatCallDuration(), formatPluralString(), formatTtl(), getString(), Locale, addRules (+18 more)

### Community 13 - "Message Service Content"
Cohesion: 0.07
Nodes (19): MessageContent, MessageService(), addedMembers, autoDeleteTime, customAction, groupTitle, m_addedMembers, m_autoDeleteTime (+11 more)

### Community 14 - "Settings"
Cohesion: 0.08
Nodes (17): hash<QString>, QSettings, Settings(), invertedTheme, invertedThemeChanged, languagePackId, languagePackIdChanged, languagePluralId (+9 more)

### Community 15 - "Chat Info Formatter"
Cohesion: 0.12
Nodes (16): formatOfflineStatus(), formatStatus(), formatUserStatus(), getMemberCountWithFallback(), handleBasicGroupUpdate(), handleChatOnlineMemberCount(), handleSupergroupUpdate(), handleUserUpdate() (+8 more)

### Community 16 - "Country Model"
Cohesion: 0.08
Nodes (26): CountryModel(), callingCode, callingCodeChanged, count, countChanged, data, fetchAndLoadCountries, formattedPhoneNumber (+18 more)

### Community 17 - "Chat Properties"
Cohesion: 0.08
Nodes (3): setLastMessage(), setPhoto(), setType()

### Community 18 - "Chat Status Formatting"
Cohesion: 0.08
Nodes (25): ChatInfoFormatter, formatOfflineStatus, formatStatus, formatUserStatus, getMemberCountWithFallback, handleBasicGroupUpdate, handleChatOnlineMemberCount, handleSupergroupUpdate (+17 more)

### Community 19 - "Chat Folder Info"
Cohesion: 0.10
Nodes (18): ChatFolderInfo(), id, m_id, m_title, Q_PROPERTY, title, ChatFolderModel(), count (+10 more)

### Community 20 - "File Download"
Cohesion: 0.09
Nodes (18): File(), canBeDownloaded, fileChanged, id, isDownloadingActive, isDownloadingCompleted, localPath, m_canBeDownloaded (+10 more)

### Community 21 - "Supergroup Entity"
Cohesion: 0.09
Nodes (15): Supergroup, activeUsernames, determineStatus, hasLocation, id, isChannel, m_activeUsernames, m_hasLocation (+7 more)

### Community 22 - "Chat List & Position"
Cohesion: 0.10
Nodes (18): ChatList, folderId, m_folderId, m_type, operator==, Q_PROPERTY, type, ChatPosition (+10 more)

### Community 23 - "Authorization Actions"
Cohesion: 0.12
Nodes (7): getCodeTypeMap(), handleAuthorizationStateWaitCode(), handleAuthorizationStateWaitOtherDeviceConfirmation(), handleAuthorizationStateWaitPassword(), handleAuthorizationStateWaitRegistration(), handleResult(), setState()

### Community 24 - "QR Code Generation"
Cohesion: 0.15
Nodes (17): Data, redundancy, size, values, Encode(), Generate(), GenerateSingle(), paint() (+9 more)

### Community 25 - "ARGS"
Cohesion: 0.21
Nodes (19): ARGS, build_openssl(), build_tdlib(), build_zlib(), check_sha256(), check_toolchain_prefix(), COLOR_BLUE, COLOR_CYAN (+11 more)

### Community 26 - "backFetching"
Cohesion: 0.13
Nodes (7): fetchMore(), fetchMoreBack(), getChatHistory(), handleChatItem(), insertMessages(), loadMessages(), roleNames()

### Community 27 - "basicGroup"
Cohesion: 0.15
Nodes (15): basicGroup(), chat(), file(), getBasicGroup(), getChat(), getFile(), getOption(), getSupergroup() (+7 more)

### Community 28 - "Authorization"
Cohesion: 0.19
Nodes (13): Authorization, ChatManager, checkInitializationStatus(), Client, close(), createLocale(), initialize(), loadLanguagePack() (+5 more)

### Community 29 - "QObject"
Cohesion: 0.18
Nodes (16): QObject, MessageAnimatedEmoji(), public, MessageContact(), public, MessageContent, MessageInvoice(), public (+8 more)

### Community 30 - "canFetchMore"
Cohesion: 0.16
Nodes (11): clear(), data(), fetchMore(), getChatPosition(), handleChatItem(), handleChatPosition(), loadChats(), populate() (+3 more)

### Community 31 - "QrCodeItem"
Cohesion: 0.12
Nodes (17): QrCodeItem, foreground, foregroundChanged, m_foreground, m_qrCodeImage, m_size, m_text, paint (+9 more)

### Community 32 - "componentComplete"
Cohesion: 0.18
Nodes (9): componentComplete(), initializeAnimation(), loadContent(), loadFileContent(), setSource(), setStatus(), stop(), updateFrame() (+1 more)

### Community 33 - "BasicGroup"
Cohesion: 0.14
Nodes (9): BasicGroup, determineStatus, id, m_id, m_memberCount, m_status, memberCount, Q_PROPERTY (+1 more)

### Community 34 - "Emoji"
Cohesion: 0.13
Nodes (7): Emoji, emojis, m_category, m_description, m_filename, m_unicode, Q_PROPERTY

### Community 35 - "callingCode"
Cohesion: 0.16
Nodes (6): data(), fetchAndLoadCountries(), get(), roleNames(), setSelectedIndex(), updatePhoneInfoFromPrefix()

### Community 36 - "QAbstractListModel"
Cohesion: 0.21
Nodes (9): QAbstractListModel, Client, data(), EmojiModel(), data, m_emojis, public, rowCount (+1 more)

### Community 37 - "Q_PROPERTY"
Cohesion: 0.17
Nodes (12): MessageAudio(), caption, duration, fileName, m_caption, m_duration, m_fileName, m_performer (+4 more)

### Community 39 - "AppManager"
Cohesion: 0.20
Nodes (11): AppManager, Authorization, Chat, ChatManager, ChatModel, Client, Locale, Message (+3 more)

### Community 40 - "Q_PROPERTY"
Cohesion: 0.25
Nodes (8): MessageCall(), discardReason, duration, isVideo, m_discardReason, m_duration, m_isVideo, Q_PROPERTY

### Community 41 - "Chat"
Cohesion: 0.38
Nodes (5): Chat, Client, LottieAnimation, Message, TDLib

### Community 42 - "BasicGroup"
Cohesion: 0.29
Nodes (6): BasicGroup, Chat, Client, StorageManager, Supergroup, User

### Community 43 - "Chat"
Cohesion: 0.29
Nodes (6): Chat, ChatManager, Client, Locale, Message, StorageManager

### Community 44 - "PluralRules_French"
Cohesion: 0.29
Nodes (3): PluralRules_French, clone, quantityForNumber

### Community 45 - "PluralRules"
Cohesion: 0.29
Nodes (6): PluralRules, clone, quantityForNumber, PluralRules_Romanian, clone, quantityForNumber

### Community 46 - "Q_PROPERTY"
Cohesion: 0.29
Nodes (7): formattedText(), MessageText(), formattedText, m_formattedText, m_text, Q_PROPERTY, text

### Community 47 - "Q_PROPERTY"
Cohesion: 0.33
Nodes (4): SupergroupFullInfo(), m_memberCount, memberCount, Q_PROPERTY

### Community 48 - "handleDeleteMessages"
Cohesion: 0.40
Nodes (6): handleDeleteMessages(), handleMessageContent(), handleMessageEdited(), handleNewMessage(), handleResult(), itemChanged()

### Community 49 - "Q_PROPERTY"
Cohesion: 0.33
Nodes (6): MessageDocument(), caption, fileName, m_caption, m_fileName, Q_PROPERTY

### Community 50 - "Q_PROPERTY"
Cohesion: 0.50
Nodes (4): MessageAnimation(), caption, m_caption, Q_PROPERTY

### Community 51 - "Q_PROPERTY"
Cohesion: 0.50
Nodes (4): MessageVenue(), m_venue, Q_PROPERTY, venue

### Community 52 - "Q_PROPERTY"
Cohesion: 0.50
Nodes (4): MessagePhoto(), caption, m_caption, Q_PROPERTY

### Community 53 - "Q_PROPERTY"
Cohesion: 0.50
Nodes (4): MessageSticker(), emoji, m_emoji, Q_PROPERTY

### Community 54 - "Q_PROPERTY"
Cohesion: 0.50
Nodes (4): MessageVideo(), caption, m_caption, Q_PROPERTY

### Community 55 - "Q_PROPERTY"
Cohesion: 0.50
Nodes (4): MessageVoiceNote(), caption, m_caption, Q_PROPERTY

### Community 56 - "MeeGram Project"
Cohesion: 0.67
Nodes (3): MeeGram Project, LottieAnimation, QrCodeItem

### Community 57 - "handleAuthorizationState"
Cohesion: 0.67
Nodes (3): handleAuthorizationState(), handleConnectionState(), handleResult()

### Community 59 - "PluralRules_Arabic"
Cohesion: 0.67
Nodes (3): PluralRules_Arabic, clone, quantityForNumber

### Community 60 - "PluralRules_Balkan"
Cohesion: 0.67
Nodes (3): PluralRules_Balkan, clone, quantityForNumber

### Community 61 - "PluralRules_Breton"
Cohesion: 0.67
Nodes (3): PluralRules_Breton, clone, quantityForNumber

### Community 62 - "PluralRules_Czech"
Cohesion: 0.67
Nodes (3): PluralRules_Czech, clone, quantityForNumber

### Community 63 - "PluralRules_Langi"
Cohesion: 0.67
Nodes (3): PluralRules_Langi, clone, quantityForNumber

### Community 64 - "PluralRules_Latvian"
Cohesion: 0.67
Nodes (3): PluralRules_Latvian, clone, quantityForNumber

### Community 65 - "PluralRules_Lithuanian"
Cohesion: 0.67
Nodes (3): PluralRules_Lithuanian, clone, quantityForNumber

### Community 66 - "PluralRules_Macedonian"
Cohesion: 0.67
Nodes (3): PluralRules_Macedonian, clone, quantityForNumber

### Community 67 - "PluralRules_Maltese"
Cohesion: 0.67
Nodes (3): PluralRules_Maltese, clone, quantityForNumber

### Community 68 - "PluralRules_None"
Cohesion: 0.67
Nodes (3): PluralRules_None, clone, quantityForNumber

### Community 69 - "PluralRules_One"
Cohesion: 0.67
Nodes (3): PluralRules_One, clone, quantityForNumber

### Community 70 - "PluralRules_Polish"
Cohesion: 0.67
Nodes (3): PluralRules_Polish, clone, quantityForNumber

### Community 71 - "PluralRules_Serbian"
Cohesion: 0.67
Nodes (3): PluralRules_Serbian, clone, quantityForNumber

### Community 72 - "PluralRules_Slovenian"
Cohesion: 0.67
Nodes (3): PluralRules_Slovenian, clone, quantityForNumber

### Community 73 - "PluralRules_Tachelhit"
Cohesion: 0.67
Nodes (3): PluralRules_Tachelhit, clone, quantityForNumber

### Community 74 - "PluralRules_Two"
Cohesion: 0.67
Nodes (3): PluralRules_Two, clone, quantityForNumber

### Community 75 - "PluralRules_Welsh"
Cohesion: 0.67
Nodes (3): PluralRules_Welsh, clone, quantityForNumber

### Community 76 - "PluralRules_Zero"
Cohesion: 0.67
Nodes (3): PluralRules_Zero, clone, quantityForNumber

## Knowledge Gaps
- **674 isolated node(s):** `COLOR_RESET`, `COLOR_RED`, `COLOR_GREEN`, `COLOR_BLUE`, `COLOR_CYAN` (+669 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **8 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `StorageManager()` connect `Storage Manager` to `Chat Photo Provider`, `Chat Manager`, `Chat Model`, `Chat Info Formatter`, `basicGroup`, `QObject`?**
  _High betweenness centrality (0.105) - this node is a cross-community bridge._
- **Why does `Chat` connect `Chat Entity` to `Chat Photo Provider`, `Chat Properties`, `Message Model`, `QObject`?**
  _High betweenness centrality (0.084) - this node is a cross-community bridge._
- **Why does `AppManager` connect `App Manager` to `Chat Photo Provider`, `handleAuthorizationState`, `Authorization`, `QObject`?**
  _High betweenness centrality (0.063) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `MessageModel` (e.g. with `chatChanged` and `result`) actually correct?**
  _`MessageModel` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `COLOR_RESET`, `COLOR_RED`, `COLOR_GREEN` to the rest of the system?**
  _674 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Chat Photo Provider` be split into smaller, more focused modules?**
  _Cohesion score 0.05230496453900709 - nodes in this community are weakly interconnected._
- **Should `Utils & Formatting` be split into smaller, more focused modules?**
  _Cohesion score 0.05603864734299517 - nodes in this community are weakly interconnected._