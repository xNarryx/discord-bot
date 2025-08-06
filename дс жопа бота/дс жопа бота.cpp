#include <dpp/dpp.h>
#include <windows.h>
#include <iostream>
#include <locale>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <limits>
#include <codecvt>
#include <mpg123.h>
#include <out123.h>
#include <curl/curl.h>
#include <SFML/Audio.hpp>
#include <random>
#include <nlohmann/json.hpp>
#include <cctype>
#include <regex>

#define VK_MEDIA_NEXT_TRACK 0xB0
#define VK_MEDIA_PREV_TRACK 0xB1
#define KEYEVENTF_KEYUP 0x0002
#define VK_MEDIA_PLAY_PAUSE 0xB3

struct Reminder {
	std::string date;
	std::string time;
	std::string user_id;
	std::string message;
};
struct Sound {
	std::string command;
	float volume;
	std::string file_path;
};
struct Server {
	dpp::snowflake guild_id;
	std::string way_leader;
	std::string way_logs;
	std::vector<dpp::snowflake> banned_ids;
	std::vector<dpp::snowflake> admin_ids;
	bool voice_all;
};
std::vector<std::wstring> yes_responses;
std::vector<std::wstring> no_responses;
static std::vector<Sound> loadSoundsFromFile(const std::string& filename) {
	std::vector<Sound> soundLibrary;
	std::ifstream file(filename);
	if (!file) {
		std::cerr << "file read error: " << filename << std::endl;
		return soundLibrary;
	}

	std::string line;
	std::locale::global(std::locale("C"));

	while (std::getline(file, line)) {
		std::istringstream iss(line);
		Sound sound;

		std::string word;
		while (iss >> word) {
			if (std::isdigit(word[0]) || word[0] == '.') {
				try {
					sound.volume = std::stof(word);
				}
				catch (const std::exception& e) {
				}
				break;
			}
			if (!sound.command.empty()) {
				sound.command += " ";
			}
			sound.command += word;
		}

		std::getline(iss >> std::ws, sound.file_path);

		if (sound.command.empty() || sound.file_path.empty()) {
			std::cerr << "Ошибка чтения строки: " << line << std::endl;
			continue;
		}

		soundLibrary.push_back(sound);
	}

	file.close();
	return soundLibrary;
}
static std::string to_utf8(const std::wstring& wstr) {
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
	std::string str_to(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str_to[0], size_needed, NULL, NULL);
	return str_to;
}
std::vector<Reminder> reminders;

std::string keep_digits(std::string str) {
	std::erase_if(str, [](unsigned char c) {
		return !std::isdigit(c);
		});
	return str;
}
static void edit_line_in_file(const std::string& filename, int line_number, const std::string& new_content) {
	std::ifstream file_in(filename);
	if (!file_in) {
		std::cerr << "Ошибка: не удалось открыть файл!" << std::endl;
		return;
	}

	std::vector<std::string> lines;
	std::string line;
	int current_line = 0;

	while (std::getline(file_in, line)) {
		if (current_line == line_number) {
			line = new_content;
		}
		lines.push_back(line);
		current_line++;
	}
	file_in.close();

	std::ofstream file_out(filename);
	if (!file_out) {
		std::cerr << "Ошибка: не удалось открыть файл для записи!" << std::endl;
		return;
	}

	for (const auto& l : lines) {
		file_out << l << "\n";
	}
	file_out.close();
}
static std::chrono::year_month_day parse_date(const std::string& date_str) {
	int day, month, year;
	char dot1, dot2;
	std::istringstream iss(date_str);
	iss >> day >> dot1 >> month >> dot2 >> year;
	return std::chrono::year_month_day{
		std::chrono::year{year} /
		std::chrono::month{static_cast<unsigned>(month)} /
		std::chrono::day{static_cast<unsigned>(day)}
	};
}
static std::string format_date(const std::chrono::year_month_day& ymd) {
	std::ostringstream oss;
	oss << std::setw(2) << std::setfill('0') << static_cast<unsigned>(ymd.day()) << '.'
		<< std::setw(2) << std::setfill('0') << static_cast<unsigned>(ymd.month()) << '.'
		<< static_cast<int>(ymd.year());
	return oss.str();
}
static std::chrono::system_clock::time_point parse_datetime(const std::string& date_str, const std::string& time_str) {
	int day, month, year, hour, minute;
	char dot1, dot2, colon;
	std::istringstream iss(date_str + " " + time_str);
	iss >> day >> dot1 >> month >> dot2 >> year >> hour >> colon >> minute;

	std::tm tm = {};
	tm.tm_mday = day;
	tm.tm_mon = month - 1;
	tm.tm_year = year - 1900;
	tm.tm_hour = hour;
	tm.tm_min = minute;

	std::time_t tt = std::mktime(&tm);
	return std::chrono::system_clock::from_time_t(tt);
}
static int getRandomNumber(std::mt19937& gen, int min, int max) {
	std::uniform_int_distribution<> distr(min, max);
	return distr(gen);
}

static void loadReminders(std::vector<Reminder>& reminders) {
	std::ifstream file("D:\\DEV\\Disbot\\reminder.txt");
	std::string date, time, user_id, message;

	reminders.clear();

	while (file >> date >> time >> user_id) {
		file.ignore();
		std::getline(file, message);
		reminders.push_back({ date, time, user_id, message });
	}
}
static void saveReminders(const std::vector<Reminder>& reminders) {
	std::ofstream file("D:\\DEV\\Disbot\\reminder.txt", std::ios::trunc);
	for (const auto& reminder : reminders) {
		file << reminder.date << " " << reminder.time << " " << reminder.user_id << " " << reminder.message << std::endl;
	}
}
static std::string getCurrentDateTime() {
	auto now = std::chrono::system_clock::now();
	std::time_t now_time = std::chrono::system_clock::to_time_t(now);

	std::tm timeinfo;
	localtime_s(&timeinfo, &now_time);

	char buffer[20];
	std::strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M", &timeinfo);

	return std::string(buffer);
}
static void checkReminders(dpp::cluster& bot) {
	loadReminders(reminders);
	auto now = std::chrono::system_clock::now();

	auto it = reminders.begin();
	while (it != reminders.end()) {
		auto reminderTime = parse_datetime(it->date, it->time);

		if (reminderTime <= now) {
			bot.direct_message_create(
				std::stoull(it->user_id),
				dpp::message(to_utf8(L"🔔 Напоминание: ") + it->message)
			);
			it = reminders.erase(it);
		}
		else {
			++it;
		}
	}

	saveReminders(reminders);
}
void SetColor(int color) {
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}
std::vector<std::string> split(const std::string& str, char delim) {
	std::stringstream ss(str);
	std::string item;
	std::vector<std::string> tokens;

	while (std::getline(ss, item, delim)) {
		if (!item.empty())
			tokens.push_back(item);
	}
	return tokens;
}

void startReminderLoop(dpp::cluster& bot) {
	std::thread([&bot]() {
		while (true) {
			checkReminders(bot);

			std::this_thread::sleep_for(std::chrono::seconds(15));
		}
		}).detach();
}
void startVoiceExp(dpp::cluster& bot, const std::vector<Server>& Servers) {
	std::thread([&bot, Servers]() {
		std::this_thread::sleep_for(std::chrono::seconds(5));
		while (true) {
			for (const auto& server : Servers) {
				if (server.way_leader.empty())
					continue;

				try {
					dpp::guild* g = dpp::find_guild(server.guild_id);
					if (!g) {
						SetColor(12);
						std::cout << to_utf8(L"Не удалось найти гильдию. ") << server.guild_id << "\n";
						SetColor(7);
						continue;
					}

					for (const auto& [user_id, state] : g->voice_members) {
						if (!state.is_self_mute() && !state.is_mute()) {
							std::ifstream file(server.way_leader);
							std::string line, leadid, leadstr;
							int linepos = 0;
							bool found = false;

							if (file.is_open()) {
								while (std::getline(file, line)) {
									int space_pos = line.find(" ");
									if (space_pos == std::string::npos) continue;
									std::vector<std::string> arg = split(line, ' ');
									dpp::snowflake leaderid = std::stoull(keep_digits(arg[0]));
									if (leaderid == user_id) {
										file.close();
										std::string rest = line.substr(space_pos + 1);
										unsigned long long num = 0;
										try {
											num = std::stoull(keep_digits(arg[1]));
										}
										catch (...) {
											std::cout << "Wrong number!" << std::endl;
											break;
										}
										int exp = static_cast<int>(num + 1);
										if (exp < 0) break;

										leadstr = keep_digits(arg[0]) + " " + std::to_string(exp);
										edit_line_in_file(server.way_leader, linepos, leadstr);
										found = true;
										break;
									}
									linepos++;
									arg.clear();
								}
								if (!found) {
									file.close();
									std::ofstream fileleader(server.way_leader, std::ios::app);
									if (fileleader.is_open()) {
										fileleader << user_id << " " << 0 << std::endl;
										SetColor(10);
										std::cout << "writing in exp " << user_id << std::endl;
										SetColor(7);
										fileleader.close();
									}
								}
							}
							else {
								std::cerr << "Cant open leaders file! " << server.way_leader << std::endl;
							}
						}
					}
				}
				catch (...) {
					SetColor(12);
					std::cout << to_utf8(L"Ошибка при обработке гильдии ") << server.guild_id << "\n";
					SetColor(7);
				}
			}
			std::this_thread::sleep_for(std::chrono::seconds(15));
		}
		}).detach();
}

static std::wstring string_to_wstring(const std::string& str) {
	int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	if (len == 0) return L"";
	std::wstring wstr(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
	wstr.pop_back();
	return wstr;
}
static std::string wstring_to_string(const std::wstring& wstr) {
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (len == 0) return "";
	std::string str(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, nullptr, nullptr);
	str.pop_back();
	return str;
}
std::vector<int16_t> decodeMP3ToPCM(const std::string& file_path, float volume = 1.0f) {
	std::vector<int16_t> pcmdata;

	// Инициализация mpg123
	mpg123_init();
	int err = 0;
	unsigned char* buffer;
	size_t buffer_size, done;
	int channels, encoding;
	long rate;

	// Создаем mpg123-хендлер
	mpg123_handle* mh = mpg123_new(NULL, &err);
	mpg123_param(mh, MPG123_FORCE_RATE, 48000, 48000.0);
	mpg123_open(mh, file_path.c_str());
	mpg123_getformat(mh, &rate, &channels, &encoding);

	buffer_size = mpg123_outblock(mh);
	buffer = new unsigned char[buffer_size];

	while (mpg123_read(mh, buffer, buffer_size, &done) == MPG123_OK) {
		int16_t* pcm = reinterpret_cast<int16_t*>(buffer);
		size_t samples = done / sizeof(int16_t);

		for (size_t i = 0; i < samples; i++) {
			float sample = pcm[i] * volume;
			int16_t clamped_sample = static_cast<int16_t>(std::max(std::min(sample, 32767.0f), -32768.0f));

			pcmdata.push_back(clamped_sample);
		}
	}

	delete[] buffer;
	mpg123_close(mh);
	mpg123_delete(mh);
	mpg123_exit();

	return pcmdata;
}
std::vector<Server> load_servers(const std::string& filename) {
	std::vector<Server> servers;

	std::ifstream file(filename);
	if (!file.is_open()) {
		std::cerr << "Не удалось открыть " << filename << std::endl;
		return servers;
	}

	nlohmann::json data;
	file >> data;

	for (auto& item : data) {
		Server s;
		s.guild_id = dpp::snowflake(item.at("guild_id").get<std::string>());
		s.way_leader = item.at("way_leader").get<std::string>();
		s.way_logs = item.at("way_logs").get<std::string>();
		for (const auto& id : item.value("banned_id", std::vector<uint64_t>{})) {
			s.banned_ids.push_back(dpp::snowflake(id));
		}

		for (const auto& id : item.value("admin_id", std::vector<uint64_t>{})) {
			s.admin_ids.push_back(dpp::snowflake(id));
		}

		s.voice_all = item.at("voiceall").get<bool>();
		servers.push_back(s);
	}

	return servers;
}
void log_message(const std::vector<Server>& servers, dpp::snowflake guild_id, dpp::snowflake author_id, const std::string& message, dpp::snowflake channel_id, bool is_banned) {
	auto it = std::find_if(servers.begin(), servers.end(), [&](const Server& s) {
		return s.guild_id == guild_id;
		});

	if (it == servers.end())
		return;
	const Server& server = *it;

	if (is_banned)
		return;
	std::cout << getCurrentDateTime() << " " << author_id << ": " << message << "   " << channel_id << " " << guild_id << std::endl;
	std::ofstream file(server.way_logs, std::ios::app);
	if (file.is_open()) {
		file << getCurrentDateTime() << " " << author_id << ": " << message << " " << channel_id << std::endl;
	}
}
std::string to_lower_utf8(const std::string& input) {
	std::wstring wstr = string_to_wstring(input);
	std::locale loc("ru_RU.UTF-8");
	for (auto& ch : wstr) {
		ch = std::tolower(ch, loc);
	}
	return wstring_to_string(wstr);
}
void save_servers_to_json(const std::string& path, const std::vector<Server>& servers) {
	nlohmann::json data = nlohmann::json::array();

	for (const auto& s : servers) {
		std::vector<uint64_t> banned, admins;
		for (auto id : s.banned_ids) banned.push_back(static_cast<uint64_t>(id));
		for (auto id : s.admin_ids) admins.push_back(static_cast<uint64_t>(id));

		data.push_back({
			{"guild_id", std::to_string(static_cast<uint64_t>(s.guild_id))},
			{"way_leader", s.way_leader},
			{"way_logs", s.way_logs},
			{"banned_id", banned},
			{"admin_id", admins},
			{"voiceall", s.voice_all}
			});
	}

	std::ofstream out(path);
	if (out.is_open()) {
		out << data.dump(4); // красиво с отступами
		out.close();
	}
}
void load_responses(const std::string& path) {
	std::ifstream in(path);
	if (!in.is_open()) return;

	nlohmann::json data;
	in >> data;

	for (const auto& str : data["yes"])
		yes_responses.push_back(string_to_wstring(str));
	for (const auto& str : data["no"])
		no_responses.push_back(string_to_wstring(str));
}
std::string normalize_units(std::string str) {
	std::map<std::string, std::string> replacements = {
		{"с", "s"}, {"С", "s"},
		{"м", "m"}, {"М", "m"},
		{"ч", "h"}, {"Ч", "h"},
		{"д", "d"}, {"Д", "d"}
	};

	for (const auto& [rus, eng] : replacements) {
		size_t pos = str.find(rus);
		if (pos != std::string::npos) {
			str.replace(pos, rus.length(), eng);
			break;
		}
	}

	return str;
}
time_t parse_duration(std::string str) {
	str = normalize_units(str);

	if (str.empty() || !isdigit(str[0])) return time(0);

	char unit = str.back();
	int value = std::stoi(str.substr(0, str.size() - 1));

	switch (unit) {
	case 's': return time(0) + value;
	case 'm': return time(0) + value * 60;
	case 'h': return time(0) + value * 60 * 60;
	case 'd': return time(0) + value * 60 * 60 * 24;
	default:  return time(0) + value * 60;
	}
}
std::string url_encode(const std::string& value) {
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex;

	for (char c : value) {
		if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
			escaped << c;
		}
		else {
			escaped << '%' << std::setw(2) << std::uppercase << int((unsigned char)c);
		}
	}

	return escaped.str();
}
template <typename Iter>
std::string join(Iter begin, Iter end, const std::string& separator) {
	std::ostringstream result;
	for (Iter it = begin; it != end; ++it) {
		if (it != begin) result << separator;
		result << *it;
	}
	return result.str();
}
std::string https_remove(std::string str) {
	std::regex link_pattern("https?://[^\\s]+");
	std::string cleaned = std::regex_replace(str, link_pattern, "");
	link_pattern = ("<:?[^\\s]+");
	cleaned = std::regex_replace(cleaned, link_pattern, "");
	return cleaned;
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
	std::ofstream* file = static_cast<std::ofstream*>(userp);
	file->write(static_cast<char*>(contents), size * nmemb);
	return size * nmemb;
}
bool generateSpeech(const std::string& text) {
	std::string apiKey = "85ae882f70bc42feabb20b913ad11dd8";

	CURL* curl = curl_easy_init();
	if (!curl) {
		std::cerr << "Ошибка инициализации CURL!" << std::endl;
		return false;
	}

	char* escaped_text = curl_easy_escape(curl, text.c_str(), 0);

	if (!escaped_text) {
		std::cerr << "Ошибка кодирования текста!" << std::endl;
		curl_easy_cleanup(curl);
		return false;
	}

	std::string url = "http://api.voicerss.org/?key=" + apiKey + "&hl=ru-ru&v=Peter&c=MP3&r=1&f=48khz_16bit_stereo&src=" + escaped_text;
	curl_free(escaped_text);

	std::ofstream file("voice.MP3", std::ios::binary);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	file.close();

	return res == CURLE_OK;
}

int main() {
	SetConsoleOutputCP(CP_UTF8);
	setlocale(LC_ALL, "ru_RU.UTF-8");
	std::string token;
	dpp::snowflake adminid = 879386342931451914;
	std::vector<Server> Servers = load_servers("D:\\DEV\\Disbot\\servers.json");
	std::ifstream filet("D:\\DEV\\Disbot\\token.txt");
	filet >> token;
	filet.close();
	bool voiceall = false;
	auto beforetimetts = std::chrono::system_clock::now();

	dpp::cluster bot(token, dpp::i_default_intents | dpp::i_message_content);

	bot.on_log(dpp::utility::cout_logger());

	bot.on_ready([&bot, Servers](const dpp::ready_t& event) {
		std::cout << "\n\nver 1.1 \n\n";
		startReminderLoop(bot);
		startVoiceExp(bot, Servers);
		});

	bot.on_voice_state_update([](const dpp::voice_state_update_t& event) {
		dpp::snowflake guild_id = event.state.guild_id;
		dpp::snowflake channel_id = event.state.channel_id;
		dpp::guild* g = dpp::find_guild(guild_id);
		auto users_vc = g->voice_members.size();
		if (users_vc <= 1) {
			event.from()->disconnect_voice(guild_id);
		}
		});

	bot.on_message_create([&bot, adminid, &voiceall, &Servers, &beforetimetts](const dpp::message_create_t& event) {
		size_t space_pos;
		unsigned long long num;
		bool finded = false;
		int linepos = 0, exp = 1, mult = 1;
		std::string date, time, leadid, leadstr, line, reply, leaderscount, message_utf8, file, filename, message, messagel;
		std::wstring wmessage;
		std::string vmessage = event.msg.content;
		dpp::snowflake author_id = event.msg.author.id;
		dpp::snowflake channel_id = event.msg.channel_id;
		dpp::snowflake guild_id = event.msg.guild_id;
		dpp::snowflake strauthor_id, leaderid, user_id, user_uid;
		std::vector<std::pair<dpp::snowflake, int>> leaders;
		std::vector<Sound> soundLibrary = loadSoundsFromFile("D:\\DEV\\Disbot\\sounds.txt");
		message = to_utf8(string_to_wstring(vmessage));
		messagel = to_lower_utf8(vmessage);
		std::random_device rd;
		std::mt19937 gen(rd());

		auto it = std::find_if(Servers.begin(), Servers.end(), [&](Server& s) {
			return s.guild_id == guild_id;
			});

		if (it == Servers.end())
			return;
		Server& server = *it;
		bool is_admin = std::find(server.admin_ids.begin(), server.admin_ids.end(), author_id) != server.admin_ids.end();
		if (author_id == 879386342931451914) {
			is_admin = true;
		}
		bool is_banned = std::find(server.banned_ids.begin(), server.banned_ids.end(), author_id) != server.banned_ids.end();

		if (!messagel.rfind(to_utf8(L"меню помощи"))) {
			if (!is_banned) {
				if (messagel == to_utf8(L"меню помощи все")) {
					is_admin = false;
				}
				std::ifstream file("D:\\DEV\\Disbot\\helpmenu.txt");
				if (file.is_open()) {
					std::string reply;
					std::string line;

					while (std::getline(file, line)) {
						if (line.find("#-") != std::string::npos && is_admin) {
							reply += "\n" + line;
						}
						else if (line.find("#+") != std::string::npos && author_id == 879386342931451914 && is_admin) {
							reply += "\n" + line;
						}
						else if (line.find("#-") == std::string::npos && line.find("#+") == std::string::npos) {
							reply += "\n" + line;
						}
					}
					event.reply(to_utf8(string_to_wstring(reply)));
					file.close();
				}
			}
			else {
				bool fs = false;
				for (auto id : server.admin_ids) {
					if (fs) {
						reply = reply + ", <@" + std::to_string(id) + ">";
						fs = true;
					}
					else {
						reply = reply + " <@" + std::to_string(id) + ">";
					}
				}
				reply = to_utf8(L"Тебе доступен только БАН. Обжалование бана -") + reply + ".";
				event.reply(reply);
			}
		}
		log_message(Servers, guild_id, author_id, message, channel_id, is_banned);
		// отключение юзеров
		if (is_banned) {
			if (author_id == 879386342931451914) {
			}
			else {
				return;
			}
		}

		// смайт
		if (messagel.substr(0, messagel.find(" ")) == to_utf8(L"смайт") || messagel.substr(0, messagel.find(" ")) == to_utf8(L"мут") && is_admin) {
			std::vector<std::string> args = split(messagel, ' ');
			if (args.size() > 3) {
				int duration = 0;
				dpp::snowflake user_id = keep_digits(args[1]);
				try { int duration = std::stoi(args[2]); }
				catch (...) {
					event.reply(to_utf8(L"Неверные аргументы."));
					return;
				}

				time_t until = parse_duration(args[2]);

				std::string reason = join(args.begin() + 3, args.end(), " ");
				reason = to_utf8(string_to_wstring(reason));
				std::cout << guild_id << "!" << user_id << "!" << until << "!" << duration << "!" << reason << "\n";

				try {
					bot.set_audit_reason(url_encode(reason))
						.guild_member_timeout(guild_id, user_id, until);
					reply = to_utf8(L"Юзер был наказан. и засунут ниже плинтуса на: **") + args[2] + "**";
					event.reply(reply);
				}
				catch (...) {
					event.reply(to_utf8(L"Что-то произошло не так..."));
				}
			}
			else {
				event.reply(to_utf8(L"Неверный синтаксис! Пример: смайт <id> <время \"с\", \"м\", \"ч\", \"д\"> <причина>"));
			}
		}
		// смена ника
		if (messagel.substr(0, messagel.find(" ")) == to_utf8(L"ник") && is_admin) {
			message = message + " ";
			std::vector<std::string> args = split(message, ' ');
			if (args.size() > 2) {
				dpp::guild_member gm;
				gm.user_id = keep_digits(args[1]);
				gm.guild_id = guild_id;
				gm.set_nickname(join(args.begin() + 2, args.end(), " "));

				bot.set_audit_reason(url_encode(to_utf8(L"Смена ника")))
					.guild_edit_member(gm);
			}
			else {
				event.reply(to_utf8(L"Неверный синтаксис! Пример: ник <id> <новый_ник>"));
			}
		}
		// prev/next music/pause
		if (messagel == "next" and is_admin) {
			event.reply("next music");
			keybd_event(VK_MEDIA_NEXT_TRACK, 0, 0, 0);
			keybd_event(VK_MEDIA_NEXT_TRACK, 0, KEYEVENTF_KEYUP, 0);
		}
		if (messagel == "prev" and is_admin) {
			event.reply("prev music");
			keybd_event(VK_MEDIA_PREV_TRACK, 0, 0, 0);
			keybd_event(VK_MEDIA_PREV_TRACK, 0, KEYEVENTF_KEYUP, 0);
		}
		if (messagel == to_utf8(L"пауза")) {
			event.reply(to_utf8(L"попытка остановить или воспроизвести"));
			keybd_event(VK_MEDIA_PLAY_PAUSE, 0, 0, 0);
			keybd_event(VK_MEDIA_PLAY_PAUSE, 0, KEYEVENTF_KEYUP, 0);
		}
		// да нет
		if (messagel.rfind(to_utf8(L"дане")) != std::string::npos and author_id != bot.me.id) {
			load_responses("D:\\DEV\\Disbot\\yesno.json");

			if (getRandomNumber(gen, 0, 1) == 1 && !no_responses.empty()) {
				event.reply(to_utf8(no_responses[getRandomNumber(gen, 0, no_responses.size() - 1)]));
			}
			else if (!yes_responses.empty()) {
				event.reply(to_utf8(yes_responses[getRandomNumber(gen, 0, yes_responses.size() - 1)]));
			}
		}
		// рандом
		if (messagel.substr(0, 8) == to_utf8(L"ранд")) {
			message = message.substr(9);
			if (!messagel.find("-")) {
				event.reply(to_utf8(L"Вам постучались из ада."));
				return;
			}
			int num = -1;
			try {
				num = std::stoull(message);
			}
			catch (...) {
				event.reply(to_utf8(L"Что-то пошло не так..."));
			}
			if (num >= 0) {
				num = getRandomNumber(gen, 0, num);
				if (num != 0) {
					reply = to_utf8(L"Выпало: **") + std::to_string(num) + "**";
					event.reply(reply);
				}
				else {
					event.reply(to_utf8(L"Выпало: я не буду крутить."));
				}
			}
		}

		// voice list users
		if (messagel == "voice" and is_admin) {
			dpp::guild* g = dpp::find_guild(guild_id);

			if (!g) {
				event.reply(to_utf8(L"Не удалось найти гильдию."));
				return;
			}

			if (g->voice_members.empty()) {
				event.reply(to_utf8(L"Никого нет в голосовых каналах."));
				return;
			}

			std::string reply = to_utf8(L"Участники в голосовых каналах:\n");

			for (const auto& [user_id, state] : g->voice_members) {
				// state.channel_id — ID голосового канала
				// можно получить пользователя по user_id
				dpp::user* user = dpp::find_user(user_id);
				std::string username = user ? user->username : std::to_string(user_id);
				std::string mute_status;

				if (state.is_self_mute() || state.is_mute()) {
					mute_status = to_utf8(L" **(замьючен)**");
				}
				reply += "- <@" + std::to_string(user_id) + ">" + mute_status + to_utf8(L" в канале <#") + std::to_string(state.channel_id) + ">\n";
			}

			event.reply(reply);
		}
		// join vc
		if (messagel.substr(0, 4) == "join" and author_id != bot.me.id) {
			dpp::guild* g = dpp::find_guild(guild_id);

			/* Attempt to connect to a voice channel, returns false if we fail to connect. */
			message = message.substr(message.find(" ") + 1);
			message = message.substr(message.find("@") + 1);
			user_uid = message.substr(0, message.find(">"));
			if (user_uid) {
				author_id = user_uid;
			}
			if (!g->connect_member_voice(author_id)) {
				event.reply(to_utf8(L"Не вижу тебя в голосовом канале!"));
			}
			else {
				event.reply(to_utf8(L"Подключилась к каналу."));
			}
			author_id = bot.me.id;
		}
		// leave vc
		if (messagel.substr(0, 5) == "leave" and author_id != bot.me.id) {
			auto g = dpp::find_guild(guild_id);
			if (g) {
				event.from()->disconnect_voice(guild_id);
				event.reply(to_utf8(L"Покинула голосовой канал."));
			}
			else {
				event.reply(to_utf8(L"Я не подключена к голосовому каналу."));
			}
		}
		// voice all
		if (messagel.substr(0, 9) == "voice all" and is_admin) {
			if (server.voice_all) {
				server.voice_all = false;
				event.reply(to_utf8(L"Выключаю озвучку всех сообщений"));
			}
			else {
				server.voice_all = true;
				event.reply(to_utf8(L"Включаю озвучку всех сообщений"));
			}
		}
		// TTS
		if (author_id != bot.me.id and messagel != "voice all") {
			if (beforetimetts >= std::chrono::system_clock::now() + std::chrono::milliseconds{ 1500 }) {
				Sleep(1000);
			}
			if (server.voice_all) {
				dpp::voiceconn* v = event.from()->get_voice(guild_id);
				if (v) {
					std::remove("voice.MP3");

					if (message.size() >= 5 and message.size() <= 120) {
						if (generateSpeech(https_remove(message))) {
						}
						else {
							std::cerr << to_utf8(L"Ошибка генерации речи! сообщение:") << message << std::endl;
						}
					}

					std::ifstream testFile("voice.MP3", std::ios::binary | std::ios::ate);
					testFile.close();

					std::string file_path = to_utf8(L"C:\\Users\\nazar\\source\\repos\\дс жопа бота\\дс жопа бота\\voice.MP3");
					std::vector<int16_t> pcmdata = decodeMP3ToPCM(file_path, 1.0f);
					v->voiceclient->send_audio_raw((uint16_t*)pcmdata.data(), pcmdata.size() * 2);
				}
			}
			else {
				if (message.substr(0, 1) == "." and author_id != bot.me.id) {
					dpp::voiceconn* v = event.from()->get_voice(guild_id);
					if (v) {
						std::remove("voice.MP3");
						message = message.substr(2);
						if (message.size() >= 5 and message.size() <= 120) {
							if (generateSpeech(https_remove(message))) {
							}
							else {
								std::cerr << to_utf8(L"Ошибка генерации речи! сообщение:") << message << std::endl;
							}
						}
						std::ifstream testFile("voice.MP3", std::ios::binary | std::ios::ate);
						testFile.close();

						std::string file_path = to_utf8(L"C:\\Users\\nazar\\source\\repos\\дс жопа бота\\дс жопа бота\\voice.MP3");
						std::vector<int16_t> pcmdata = decodeMP3ToPCM(file_path, 1.0f);
						v->voiceclient->send_audio_raw((uint16_t*)pcmdata.data(), pcmdata.size() * 2);
					}
				}
			}
			beforetimetts = std::chrono::system_clock::now();
		}

		//stop audio
		if (messagel.substr(0, 4) == "skip" || messagel.substr(0, 4) == "stop") {
			if (message.substr(0, 4) == "stop") {
				for (int i = 0; i++; i > 10) {
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
					dpp::voiceconn* v = event.from()->get_voice(guild_id);
					v->voiceclient->stop_audio();
				}
			}
			dpp::voiceconn* v = event.from()->get_voice(guild_id);
			v->voiceclient->stop_audio();
		}
		// Soundpad
		if (message.substr(0, 1) == ",") {
			message = message.substr(1);
			dpp::voiceconn* v = event.from()->get_voice(guild_id);
			if (v) {
				for (const auto& sound : soundLibrary) {
					if (message == sound.command) {
						std::cout << "reading sound\n";
						std::vector<int16_t> pcmdata = decodeMP3ToPCM(sound.file_path, sound.volume);
						v->voiceclient->send_audio_raw((uint16_t*)pcmdata.data(), pcmdata.size() * 2);
						return;
					}
				}
			}
		}
		//sound list
		if (messagel == "soundlist" and author_id != bot.me.id) {
			std::string replymessage;

			for (const auto& sound : soundLibrary) {
				replymessage = replymessage + "> " + sound.command + "\n";
			}
			replymessage = to_utf8(L"## Список звуков:\n") + replymessage;
			event.reply(to_utf8(string_to_wstring(replymessage)));
		}

		// leaders
		if (messagel.substr(0, 12) == to_utf8(L"лидеры") and author_id != bot.me.id) {
			int leaderssize = 10;
			if (message.substr(message.find(" ") + 1) == to_utf8(L"все") and author_id != bot.me.id) {
				leaderssize = 99;
			}
			filename = server.way_leader;
			std::ifstream file(filename);
			if (!file) {
				event.reply(to_utf8(L"У вас не зарегистрирован список лидеров."));
				return;
			}
			while (file >> user_id >> exp) {
				leaders.emplace_back(user_id, exp);
			}
			std::sort(leaders.begin(), leaders.end(), [](const auto& a, const auto& b) {
				return a.second > b.second;
				});
			for (int i = 0; i < leaders.size() and i < leaderssize; i++) {
				leaderscount = leaderscount + "> " + std::to_string(i + 1) + "# <@" + std::to_string(leaders[i].first) + "> " + std::to_string(leaders[i].second) + "\n";
			}
			reply = to_utf8(wmessage = L"## Список лидеров по экспе на текущий момент: ");
			reply = reply + "\n" + leaderscount;
			event.reply(reply);
			leaderscount = "";
		}

		// id - xp - timevc
		if (author_id != bot.me.id && !is_banned) {
			filename = server.way_leader;

			std::ifstream file(filename);
			if (file.is_open()) {
				linepos = 0;
				while (std::getline(file, line)) {
					std::vector<std::string> args = split(line, ' ');
					dpp::snowflake leaderid = std::stoull(keep_digits(args[0]));
					if (leaderid == author_id) {
						file.close();
						mult = static_cast<int>(std::floor(std::log2(message.size() + 1)));
						try {
							num = std::stoull(keep_digits(args[1]));
						}
						catch (...) {
							std::cout << "Wrong number!" << std::endl;
							return;
						}

						if (num > std::numeric_limits<unsigned long long>::max() - mult) {
							SetColor(6);
							std::cout << "Error: experience overflow detected!" << std::endl;
							SetColor(7);
							return;
						}
						exp = num + mult;
						if (exp < 0) {
							SetColor(6);
							std::cout << "\nError: experience overflow detected!\n" << std::endl;
							SetColor(7);
							return;
						}
						leadstr = keep_digits(args[0]) + " " + std::to_string(exp);
						edit_line_in_file(filename, linepos, leadstr);
						linepos = 1;
						finded = true;
						if (guild_id == 1346922961759633479) {
							if (exp >= 71000 * 2) {
								bot.guild_member_add_role(guild_id, author_id, 1344210310352867328); //50
							}
							else {
								if (exp >= 47000 * 2) {
									bot.guild_member_add_role(guild_id, author_id, 1344210251104256082); //45
								}
								else {
									if (exp >= 32000 * 2) {
										bot.guild_member_add_role(guild_id, author_id, 1344210201108025404); //40
									}
									else {
										if (exp >= 21200 * 2) {
											bot.guild_member_add_role(guild_id, author_id, 1344210171311820905); // 35
										}
										else {
											if (exp >= 14128 * 2) {
												bot.guild_member_add_role(guild_id, author_id, 1371474135498100766); // 30
											}
											else {
												if (exp >= 9500 * 2) {
													bot.guild_member_add_role(guild_id, author_id, 1371474094402310204); // 25
												}
												else {
													if (exp >= 6200 * 2) {
														bot.guild_member_add_role(guild_id, author_id, 1371474046293901343); // 20
													}
													else {
														if (exp >= 4200 * 2) {
															bot.guild_member_add_role(guild_id, author_id, 1371474010537332806); // 15
														}
														else {
															if (exp >= 2700 * 2) {
																bot.guild_member_add_role(guild_id, author_id, 1371473915188351076); // 10
															}
															else {
																if (exp >= 1800 * 2) {
																	bot.guild_member_add_role(guild_id, author_id, 1371473304535306260); // 5
																}
																else {
																	if (exp >= 500 * 2) { //0
																		bot.guild_member_add_role(guild_id, author_id, 1376609034706354218); // 1
																	}
																}
															}
														}
													}
												}
											}
										}
									}
								}
							}
						}
					}
					linepos++;
					args.clear();
				}
				if (!finded) {
					file.close();
					std::ofstream fileleader(filename, std::ios::app);
					if (fileleader.is_open()) {
						fileleader << author_id << " " << 0 << std::endl;
						SetColor(10);
						std::cout << "writing in exp " << author_id << std::endl;
						SetColor(7);
						file.close();
					}
					finded = false;
				}
			}
			else {
				std::cerr << "Cant open leaders file!" << filename << std::endl;
			}
		}

		// exp <id>
		if (messagel.substr(0, messagel.find(" ") + 1) == to_utf8(L"опыт ") and author_id != bot.me.id) {
			linepos = 0;
			filename = server.way_leader;
			std::ifstream file(filename);
			message = message.substr(message.find(" ") + 1);
			message = message.substr(message.find("@") + 1);
			strauthor_id = message.substr(0, message.find(">"));
			std::cout << "!" << strauthor_id << "!\n";
			try {
				exp = std::stoull(message.substr(message.find(" ") + 1));
				if (exp > 1002921866) {
					exp = 0;
					std::cout << "!" << exp << "!\n";
				}
				else {
					std::cout << "!" << exp << "!\n";
				}
			}
			catch (...) {
			}
			if (file.is_open()) {
				finded = false;
				while (std::getline(file, line)) {
					space_pos = line.find(" ");
					leadid = line.substr(0, space_pos);
					try {
						leaderid = std::stoull(leadid);
					}
					catch (...) {
					}
					if (leaderid == strauthor_id && exp > 0 && is_admin) {
						leadstr = leadid + " " + std::to_string(exp);
						std::cout << leadstr << "\n";
						edit_line_in_file(filename, linepos, leadstr);
						file.close();
						line = line.substr(space_pos + 1);
						reply = to_utf8(L"> Опыт пользователя теперь: **") + std::to_string(exp) + "**";
						event.reply(reply);

						linepos = 1;
						finded = true;
					}
					else if (leaderid == strauthor_id) {
						file.close();
						line = line.substr(space_pos + 1);
						reply = to_utf8(L"> Опыт пользователя: **") + line + "**";
						event.reply(reply);

						linepos = 1;
						finded = true;
					}
					linepos++;
				}
				if (!finded) {
					file.close();
					event.reply("Cant find stats");
					finded = false;
					return;
				}
			}
			else {
				std::cerr << "Cant open leaders file!" << std::endl;
			}
		}
		// myexp
		if (messagel.substr(0, messagel.find(" ") + 9) == to_utf8(L"мой опыт") and author_id != bot.me.id) {
			filename = server.way_leader;
			std::ifstream file(filename);
			if (file.is_open()) {
				finded = false;
				while (std::getline(file, line)) {
					space_pos = line.find(" ");
					leadid = line.substr(0, space_pos);
					leaderid = std::stoull(leadid);
					if (leaderid == author_id) {
						file.close();
						line = line.substr(space_pos + 1);
						exp = std::stoull(line);

						reply = to_utf8(L"> Твой опыт: **") + line + "**";
						event.reply(reply);

						linepos = 1;
						finded = true;
					}
					linepos++;
				}
				if (!finded) {
					file.close();
					event.reply("Cant find your stats");
					finded = false;
					return;
				}
			}
			else {
				std::cerr << "Cant open leaders file!" << std::endl;
			}
		}

		// Reminder writer
		if (messagel.substr(0, messagel.find(" ") + 1) == to_utf8(L"напомни ") and author_id != bot.me.id) {
			space_pos = message.find(' '); // command
			message = message.substr(space_pos + 1);

			space_pos = message.find(' '); // date
			if (message.substr(0, space_pos) == to_utf8(L"завтра")) {
				std::chrono::year_month_day ymd = parse_date(getCurrentDateTime().substr(0, space_pos));
				std::chrono::sys_days sd = std::chrono::sys_days{ ymd };
				sd += std::chrono::days{ 1 };
				std::chrono::year_month_day new_ymd = std::chrono::year_month_day{ sd };
				date = format_date(new_ymd);
				std::cout << date << ".\n";
				message = message.substr(space_pos + 1);
			}
			else if (message.substr(0, space_pos) == to_utf8(L"сегодня")) {
				date = getCurrentDateTime().substr(0, space_pos - 4);
				std::cout << date << ".\n";
				message = message.substr(space_pos + 1);
			}
			else if (message.substr(0, space_pos) == to_utf8(L"неделю")) {
				std::chrono::year_month_day ymd = parse_date(getCurrentDateTime().substr(0, space_pos));
				std::chrono::sys_days sd = std::chrono::sys_days{ ymd };
				sd += std::chrono::days{ 7 };
				std::chrono::year_month_day new_ymd = std::chrono::year_month_day{ sd };
				date = format_date(new_ymd);
				std::cout << date << ".\n";
				message = message.substr(space_pos + 1);
			}
			else {
				date = message.substr(0, space_pos);
				message = message.substr(space_pos + 1);
			}

			space_pos = message.find(' '); // time
			time = message.substr(0, space_pos);
			message = message.substr(space_pos + 1);
			std::cout << time << ".\n";

			space_pos = message.find(' '); // message
			if (date.size() == 10 and time.size() == 5) {
				std::ofstream file("D:\\DEV\\Disbot\\reminder.txt", std::ios::app);
				if (file.is_open()) {
					file << date << " " << time << " " << author_id << " " << message << std::endl;
					file.close();
					event.reply(to_utf8(L"Записала! Жди напомианаени!!"));
					std::cout << "All addet" << std::endl;
				}
				else {
					std::cerr << "Cant open file!" << std::endl;
					event.reply("Cant open file!");
				}
			}
			else {
				event.reply(to_utf8(L"Ты указал неверное время/дату"));
			}

			date, time = "";
		}

		// Отсылка сообщений юзерам по айди
		if (messagel.find("send to") != std::string::npos && author_id == 879386342931451914 and author_id != bot.me.id) {
			if (message.find("send to user") != std::string::npos) {
				message = message.substr(13);
				space_pos = message.find(' ');
				if (space_pos == std::string::npos) {
					bot.message_create(dpp::message(channel_id, "Wrong args"));
					return;
				}
				std::string channel_id_str = message.substr(0, space_pos);
				std::string messagesent = message.substr(space_pos + 1);
				event.reply("trying to send...", false);
				try {
					dpp::snowflake channel_idsent = std::stoull(channel_id_str);
					bot.direct_message_create(channel_idsent, dpp::message(messagesent));
					bot.message_create(dpp::message(channel_id, "Message sent"));
				}
				catch (...) {
					bot.message_create(dpp::message(channel_id, "Invalid channel ID"));
					std::cout << "wrong channel id: " << channel_id_str << std::endl;
					return;
				}
			}
			else {
				message = message.substr(8);
				space_pos = message.find(' ');
				if (space_pos == std::string::npos) {
					bot.message_create(dpp::message(channel_id, "Wrong args"));
					return;
				}
				std::string channel_id_str = message.substr(0, space_pos);
				std::string messagesent = message.substr(space_pos + 1);
				event.reply("trying to send...", false);
				try {
					dpp::snowflake channel_idsent = std::stoull(channel_id_str);
					bot.message_create(dpp::message(channel_idsent, messagesent));
					bot.message_create(dpp::message(channel_id, "Message sent"));
				}
				catch (...) {
					bot.message_create(dpp::message(channel_id, "Invalid channel ID"));
					std::cout << "wrong channel id: " << channel_id_str << std::endl;
					return;
				}
			}
		}

		// Админ
		if (messagel == to_utf8(L"админ")) {
			if (is_admin) {
				event.reply(to_utf8(L"Да, вы администратор."));
			}
			else {
				event.reply(to_utf8(L"Да, вы **НЕ** администратор."));
			}
		}
		// бан лист и бан
		if (messagel.substr(0, messagel.find(" ")) == to_utf8(L"бан")) {
			if (messagel == to_utf8(L"бан лист")) {
				reply = " ";
				for (auto& id : server.banned_ids) {
					reply = reply + "> <@" + std::to_string(id) + ">\n";
				}
				reply = to_utf8(L"Забаненые айди для бота : \n") + reply;
				event.reply(reply);
			}
			if (messagel.substr(0, messagel.find(" ")) == to_utf8(L"бан") && is_admin) {
				if (std::stoull(keep_digits(messagel)) != 0) {
					server.banned_ids.push_back(keep_digits(messagel));
					event.reply(to_utf8(L"Юзер был отключен от пользования ботом"));
					save_servers_to_json("D:\\DEV\\Disbot\\servers.json", Servers);
				}
			}
		}
		if (messagel.substr(0, messagel.find(" ")) == to_utf8(L"разбан") && is_admin) {
			uint64_t id_to_unban = std::stoull(keep_digits(messagel));

			if (id_to_unban != 0) {
				auto& vec = server.banned_ids;
				auto new_end = std::remove(vec.begin(), vec.end(), id_to_unban);
				if (new_end != vec.end()) {
					vec.erase(new_end, vec.end());
					event.reply(to_utf8(L"Разбанен."));
					save_servers_to_json("D:\\DEV\\Disbot\\servers.json", Servers);
				}
				else {
					event.reply(to_utf8(L"ID не найден в бан-листе."));
				}
			}
		}
		// admin
		if (messagel.substr(0, messagel.find(" ")) == to_utf8(L"админ")) {
			if (messagel == to_utf8(L"админ лист")) {
				reply = " ";
				for (auto& id : server.admin_ids) {
					reply = reply + "> <@" + std::to_string(id) + ">\n";
				}
				reply = to_utf8(L"Администрация этого сервера : \n") + reply;
				event.reply(reply);
			}
			if (messagel.substr(0, messagel.find(" ") + 17) == to_utf8(L"админ добавить") && is_admin) {
				if (std::stoull(keep_digits(messagel)) != 0) {
					server.admin_ids.push_back(keep_digits(messagel));
					event.reply(to_utf8(L"Юзер был повышен до статуса **администратора** на этом сервере."));
					save_servers_to_json("D:\\DEV\\Disbot\\servers.json", Servers);
				}
			}
		}
		if (messagel.substr(0, messagel.find(" ") + 11) == to_utf8(L"админ снять") && is_admin) {
			uint64_t id_to_unadmin = std::stoull(keep_digits(messagel));

			if (id_to_unadmin != 0) {
				auto& vec = server.admin_ids;
				auto new_end = std::remove(vec.begin(), vec.end(), id_to_unadmin);
				if (new_end != vec.end()) {
					vec.erase(new_end, vec.end());
					event.reply(to_utf8(L"Пользователь был понижен до обычного юзера."));
					save_servers_to_json("D:\\DEV\\Disbot\\servers.json", Servers);
				}
				else {
					event.reply(to_utf8(L"ID не найден в Админ-листе."));
				}
			}
		}
		// статус
		if (messagel.substr(0, messagel.find(" ")) == to_utf8(L"статус") && author_id == 879386342931451914) {
			message = message.substr(message.find(" ") + 1);

			bot.set_presence(dpp::presence(dpp::ps_dnd, dpp::at_watching, message));
			event.reply(to_utf8(L"Сменила статус"));
		}
		});

	bot.start(dpp::st_wait);
	mpg123_exit();
	return 0;
}