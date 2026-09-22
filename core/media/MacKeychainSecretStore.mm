#include "MacKeychainSecretStore.h"

#import <Security/Security.h>

namespace {

const CFStringRef kSourceAccountService = CFSTR("com.bronekox.QueMusic.source-account");

void setKeychainError(OSStatus status, QString *error)
{
    if (error) {
        *error = QStringLiteral("macOS Keychain operation failed (%1)").arg(status);
    }
}

CFMutableDictionaryRef queryForReference(const QString &reference)
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const CFStringRef account = reference.toCFString();
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, kSourceAccountService);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFRelease(account);
    return query;
}

}

bool MacKeychainSecretStore::write(const QString &reference, const QByteArray &secret, QString *error)
{
    if (reference.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Secret reference is required");
        }
        return false;
    }

    CFMutableDictionaryRef query = queryForReference(reference);
    SecItemDelete(query);
    const CFDataRef data = CFDataCreate(kCFAllocatorDefault,
                                        reinterpret_cast<const UInt8 *>(secret.constData()), secret.size());
    CFDictionarySetValue(query, kSecValueData, data);
    CFDictionarySetValue(query, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
    const OSStatus status = SecItemAdd(query, nullptr);
    CFRelease(data);
    CFRelease(query);
    if (status == errSecSuccess) {
        return true;
    }
    setKeychainError(status, error);
    return false;
}

std::optional<QByteArray> MacKeychainSecretStore::read(const QString &reference, QString *error) const
{
    if (reference.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Secret reference is required");
        }
        return std::nullopt;
    }

    CFMutableDictionaryRef query = queryForReference(reference);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status != errSecSuccess) {
        setKeychainError(status, error);
        return std::nullopt;
    }

    const CFDataRef data = static_cast<CFDataRef>(result);
    const QByteArray secret(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), CFDataGetLength(data));
    CFRelease(result);
    return secret;
}

bool MacKeychainSecretStore::remove(const QString &reference, QString *error)
{
    if (reference.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Secret reference is required");
        }
        return false;
    }

    CFMutableDictionaryRef query = queryForReference(reference);
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    if (status == errSecSuccess || status == errSecItemNotFound) {
        return true;
    }
    setKeychainError(status, error);
    return false;
}
