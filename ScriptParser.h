#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct Scene;

struct ScriptParserError {
	ScriptParserError(std::string message) : message(std::move(message)) {}
	std::string message;
};

class ScriptParser
{
public:
	ScriptParser(const Scene& scene);
	~ScriptParser();
	void parseFile(const std::string& scriptFilePath);

	struct ImportedProperty {
		std::string type;
		std::string name;
		std::string defaultValue;
	};
	struct Script {
		std::string superScript;
		std::vector<ImportedProperty> importedProperties;
	};
	std::map<std::string, Script> scripts;
	std::map<std::string, std::string> typeAliasMap;
	std::string lastScript;

	std::string getNativeImportPropertyList(const std::string& scriptName) const;

private:
	const Scene& scene;
	void* zip;
};
