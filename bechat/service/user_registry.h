#ifndef BECHAT_SERVICE_USER_REGISTRY_H_
#define BECHAT_SERVICE_USER_REGISTRY_H_

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

struct UserRecord {
  std::string username;
  std::string password_hash;
};

class UserRegisty {
 public:
  /**
   * @brief 注册用户
   *
   * @param username 用户名
   * @param password 密码
   * @return 状态码
   */
  int Signup(const std::string& username, const std::string& password) {
    // 1. 查找 user 是否已经注册
    // 2. 计算 password 哈希
    // 3. 存储用户记录
    // 4. 返回状态码
    return 1;
  }

  /**
   * @brief 验证用户
   *
   * @param username 用户名
   * @param password 密码
   * @return 状态码
   */
  int Verify(const std::string& username, const std::string& password) {
    // 1. 查找 user 是否已经注册
    // 2. 对比 password 哈希
    // 3. 返回状态码
    return 1;
  }

  // // 注册新用户：传入原始密码，返回可存入数据库的哈希字符串
  // std::string hashPassword(const std::string& password) {
  //   char hashed[crypto_pwhash_STRBYTES];
  //   if (crypto_pwhash_str(hashed, password.c_str(), password.length(),
  //                         crypto_pwhash_OPSLIMIT_INTERACTIVE,
  //                         crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
  //     throw std::runtime_error("Password hashing failed (out of memory?)");
  //   }
  //   return std::string(hashed);
  // }

  // // 登录验证：从数据库取出的哈希 与 用户输入的密码 比对
  // bool verifyPassword(const std::string& stored_hash,
  //                     const std::string& input_password) {
  //   return crypto_pwhash_str_verify(stored_hash.c_str(),
  //   input_password.c_str(),
  //                                   input_password.length()) == 0;
  // }

 private:
  std::unordered_map<std::string /* username */, UserRecord> user_records_;
  std::mutex user_records_mtx_;
};

#endif  // !BECHAT_SERVICE_USER_REGISTRY_H_
