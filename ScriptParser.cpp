#include "ScriptParser.h"
#include "gameobj.h"
#include <fmt/format.h>
#include <miniz/miniz.h>
#include <regex>

static std::string toLower(std::string str)
{
	for (char& c : str)
		if (c >= 'A' && c <= 'Z')
			c = c - 'A' + 'a';
	return str;
};

struct Tokenizer
{
	const char* ptr;
	const char* endPtr;

	static bool isNewLine(char c) { return  c == '\r' || c == '\n'; }
	static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
	static bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
	static bool isDigit(char c) { return (c >= '0' && c <= '9'); }
	static bool isAlphanumeric(char c) { return isLetter(c) || isDigit(c); }
	static bool isNumeric(char c) { return isDigit(c) || c == '.'; }
	static bool isParenthesis(char c) { return c == '(' || c == '[' || c == '{' || c == '}' || c == ']' || c == ')'; }
	static bool isSeparator(char c) { return c == ';' || c == ',' || c == '.'; }
	static bool isSign(char c) {
		return	c == '-' || c == '+' || c == '*' || c == '/' || c == '.' ||
			c == '=' || c == '!' || c == '&' || c == '|' || c == '^' ||
			c == '<' || c == '>' || c == ':';
	}
	
	Tokenizer(const char* ptr, size_t len) : ptr(ptr), endPtr(ptr + len) {}

	enum TokenType {
		Unknown = 0,
		Identifier,
		NumberLiteral,
		StringLiteral,
		Parenthesis,
		Separator,
		Operator,
		Preprocessor,
	};

	bool more() const {
		return ptr < endPtr;
	}

	std::pair<std::string_view, TokenType> nextToken()
	{
		while (more() && isSpace(*ptr)) ++ptr;
		const char* beg = ptr;
		// first char:
		//  letter, _     -> identifier
		//  digit         -> number literal
		//  "             -> string literal
		//  ([{}])        -> parenthesis
		//  commas, signs -> operator
		//  #			  -> "preprocessor" stuff
		if (!more()) {
			// empty
			return { {}, Unknown };
		}
		else if (isLetter(*beg)) {
			// identifier
			ptr += 1;
			while (more() && isAlphanumeric(*ptr))
				ptr += 1;
			return { std::string_view(beg, ptr - beg), Identifier };
		}
		else if (isDigit(*beg)) {
			// number literal
			ptr += 1;
			while (more() && isNumeric(*ptr))
				ptr += 1;
			return { std::string_view(beg, ptr - beg), NumberLiteral };
		}
		else if (*beg == '"') {
			// string literal
			ptr += 1;
			while (more()) {
				if (*ptr == '"') {
					ptr += 1;
					return { std::string_view(beg, ptr - beg), StringLiteral };
				}
				ptr += 1;
			}
			throw ScriptParserError("end of file reached but string is not terminated");
		}
		else if (isParenthesis(*beg) || isSeparator(*beg)) {
			ptr += 1;
			return { std::string_view(beg, ptr - beg), isParenthesis(*beg) ? Parenthesis : Separator };
		}
		else if (*beg == '/') {
			ptr += 1;
			if (*ptr == '/') {
				// comment, stop until newline
				while (more() && *ptr != '\n')
					ptr += 1;
				return nextToken();
			}
			else if (*ptr == '*') {
				/* comment, stop until */
				char prev = 0;
				ptr += 1;
				while (more() && !(prev == '*' && *ptr == '/')) {
					prev = *ptr;
					ptr += 1;
				}
				if (more()) {
					ptr += 1;
				}
				return nextToken();
			}
			else {
				return { std::string_view(beg, ptr - beg), Operator };
			}
		}
		else if (isSign(*beg)) {
			// operator
			ptr += 1;
			while (more() && isSign(*ptr))
				ptr += 1;
			return { std::string_view(beg, ptr - beg), Operator };
		}
		else if (*beg == '#') {
			ptr += 1;
			while (more() && isLetter(*ptr))
				ptr += 1;
			return { std::string_view(beg, ptr - beg), Preprocessor };
		}
		else {
			throw ScriptParserError(fmt::format("unknown character '{}'", *beg));
		}
	}

	static TokenType getTokenType(std::string_view token) {
		if (token.empty()) return Unknown;
		if (isLetter(token[0])) return Identifier;
		if (isDigit(token[0])) return NumberLiteral;
		if (token[0] == '"') return StringLiteral;
		if (isParenthesis(token[0])) return Parenthesis;
		if (isSeparator(token[0])) return Separator;
		if (isSign(token[0])) return Operator;
		if (token[0] == '#') return Preprocessor;
		return Unknown;
	}
};

ScriptParser::ScriptParser(const Scene& scene) : scene(scene)
{
	mz_zip_archive* mzZip = (mz_zip_archive*)malloc(sizeof(mz_zip_archive));
	mz_zip_zero_struct(mzZip);
	mz_bool mzreadok = mz_zip_reader_init_mem(mzZip, scene.zipmem.data(), scene.zipmem.size(),
		MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY | MZ_ZIP_FLAG_VALIDATE_LOCATE_FILE_FLAG);
	assert(mzreadok);
	zip = mzZip;
}

ScriptParser::~ScriptParser()
{
	mz_zip_reader_end((mz_zip_archive*)zip);
	free(zip);
}

//std::string parseNativeImportsFromScript(const Scene& scene, const std::string& scriptFilePath)
void ScriptParser::parseFile(const std::string& scriptFilePath)
{
	struct ZippedFile {
		void* bytes = nullptr; size_t length = 0;
		ZippedFile(mz_zip_archive* zip, const char* path) {
			bytes = mz_zip_reader_extract_file_to_heap(zip, path, &length, 0);
			if (!bytes)
				throw ScriptParserError(fmt::format("Could not find file {}", path));
		}
		~ZippedFile() { if (bytes) free(bytes); }
	};

	std::string processedFilePath = scriptFilePath;
	for (char& c : processedFilePath)
		if (c == '\\')
			c = '/';
	static const std::regex multiSlashes(R"---(\/\/+)---");
	static const std::regex removeDotDot(R"---([^./]+\/\.\.\/)---");
	processedFilePath = std::regex_replace(processedFilePath, multiSlashes, "/");
	processedFilePath = std::regex_replace(processedFilePath, removeDotDot, "");

	ZippedFile file((mz_zip_archive*)zip, processedFilePath.c_str());
	Tokenizer tok((const char*)file.bytes, file.length);

	auto nextTokenAsType = [&tok](Tokenizer::TokenType expectedType)
		{
			auto [token, type] = tok.nextToken();
			if (type != expectedType) {
				throw ScriptParserError("unexpected token type");
			}
			if (type == Tokenizer::StringLiteral)
				token = token.substr(1, token.size() - 2);
			return token;
		};
	auto expectToken = [&tok](std::string_view expectedToken)
		{
			auto [token, type] = tok.nextToken();
			if (token != expectedToken) {
				throw ScriptParserError(fmt::format("unexpected token {}, expected {}", token, expectedToken));
			}
		};

	auto leaveScope = [&tok]()
		{
			int numBraces = 1;
			auto [token, _] = tok.nextToken();
			while (!token.empty()) {
				if (token == "{") {
					numBraces += 1;
				}
				else if (token == "}") {
					numBraces -= 1;
					if (numBraces == 0)
						return;
				}
			}
		};


	auto sepIndex = processedFilePath.find_last_of("/\\");
	auto folder = sepIndex != processedFilePath.npos ? processedFilePath.substr(0, sepIndex + 1) : std::string();

	while (true) {
		auto [tokenView, type] = tok.nextToken();
		if (tokenView.empty()) {
			break;
		}
		std::string token(tokenView);
		token = toLower(std::move(token));
		if (token == "#include") {
			auto includedFile = nextTokenAsType(Tokenizer::StringLiteral);
			parseFile(folder + std::string(includedFile));
		}
		else if (token == "script") {
			std::string name;
			std::tie(name, type) = tok.nextToken();
			assert(type == Tokenizer::Identifier);
			auto& script = scripts[name];
			lastScript = name;
			std::tie(token, type) = tok.nextToken();
			if (token == "extends") {
				script.superScript = nextTokenAsType(Tokenizer::Identifier);
				std::tie(token, type) = tok.nextToken();
			}
			if (token != "{") {
				throw ScriptParserError("{ expected to define script");
			}
			std::tie(token, type) = tok.nextToken();
			if (token == "NativeImport") {
				expectToken("{");
				std::tie(token, type) = tok.nextToken();
				while (!token.empty() && token != "}") {
					ImportedProperty& var = script.importedProperties.emplace_back();
					var.type = token;
					var.name = nextTokenAsType(Tokenizer::Identifier);
					std::tie(token, type) = tok.nextToken();
					if (token == "=") {
						std::tie(token, type) = tok.nextToken();
						var.defaultValue = std::move(token);
						expectToken(";");
					}
					else if (token != ";") {
						throw ScriptParserError("; expected to finish variable definiton");
					}
					std::tie(token, type) = tok.nextToken();
				}
			}
			else {
				printf("Script does not have NativeImport at start. I assume it does not have one.\n");

			}
		}
		else if (token == "nativetypealias") {
			expectToken("{");
			std::tie(token, type) = tok.nextToken();
			while (!token.empty() && token != "}") {
				assert(type == Tokenizer::Identifier);
				token = toLower(std::move(token));
				std::string typeName(token);
				auto nativeType = nextTokenAsType(Tokenizer::Identifier);
				// NativeType or NativeObject
				if (nativeType == "NativeType") {
					expectToken("(");
					auto primType = nextTokenAsType(Tokenizer::Identifier);
					expectToken(",");
					auto zname = nextTokenAsType(Tokenizer::StringLiteral);
					expectToken(")");
					expectToken(";");
					typeAliasMap[typeName] = zname;
				}
				else if (nativeType == "NativeObject") {
					expectToken("(");
					auto className = nextTokenAsType(Tokenizer::StringLiteral);
					expectToken(",");
					auto refName = nextTokenAsType(Tokenizer::StringLiteral);
					expectToken(",");
					auto ifName = nextTokenAsType(Tokenizer::StringLiteral);
					expectToken(")");
					expectToken(";");
					typeAliasMap[typeName] = refName;
				}
				else {

				}


				std::tie(token, type) = tok.nextToken();
			}
			;
		}
	}
}

std::string ScriptParser::getNativeImportPropertyList(const std::string& scriptName) const
{
	auto itScript = scripts.find(scriptName);
	if (itScript == scripts.end())
		throw ScriptParserError(fmt::format("Could not find script class {}", scriptName));
	const auto& script = itScript->second;
	std::string result;
	if (!script.superScript.empty()) {
		result = getNativeImportPropertyList(script.superScript);
	}
	for (const auto& var : script.importedProperties) {
		auto aliasIt = typeAliasMap.find(toLower(var.type));
		if (aliasIt != typeAliasMap.end())
			result += aliasIt->second;
		else
			result += var.type;
		result += ' ';
		result += var.name;
		if (!var.defaultValue.empty()) {
			result += '=';
			result += var.defaultValue;
		}
		result += ';';
	}
	return result;
}
