#include "WindowsRegistry.h"

#ifdef _WIN32

#include "common/Exception.h"

#include <string>
#include <windows.h>

static std::string to_string(const std::wstring& s) {
	if (s.empty())
		return {};
	int size = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
	if (size <= 0)
		return {};
	std::string result(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), size, nullptr, nullptr);
	return result;
}

static std::wstring to_wstring(const std::string& s) {
	if (s.empty())
		return {};
	int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	if (size <= 0)
		return {};
	std::wstring result(size, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), size);
	return result;
}

std::string registry_get_string_value(RegistryRootKey root_, const std::string& key_name_, const std::string& value_name_) {
	HKEY root = nullptr;
	switch (root_) {
		case REGISTRY_HKEY_LOCAL_MACHINE:
			root = HKEY_LOCAL_MACHINE;
	}
	if (root == nullptr) {
		throw Exception("registry_get_string_value: unknown root_key");
	}

	std::wstring key_name = to_wstring(key_name_);
	std::wstring value_name = to_wstring(value_name_);

	DWORD data_size = 0;
	LSTATUS result = RegGetValueW(root, key_name.c_str(), value_name.c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &data_size);
	if (result != ERROR_SUCCESS) {
		// TODO: throw Execption? return Optional?
		return "";
	}

	std::wstring data;
	data.resize(data_size);

	result = RegGetValueW(root, key_name.c_str(), value_name.c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, (void*)data.data(), &data_size);

	if (result != ERROR_SUCCESS) {
		// TODO: throw Execption? return Optional?
		return "";
	}

	data.erase(data.find(L'\0'));
	return to_string(data);
}

#endif
