#include "cxxopts.hpp"
#include "dbbench/benchmarks/tatp.hpp"
#include "dbbench/runner.hpp"
#include "helpers.hpp"
#include "sqlite3.hpp"

#include <sys/mman.h>
#include <sls.h>
#include <sls_wal.h>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <utility>

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

#define URI_MAXLEN (4096)

void load(sqlite::Connection &conn, uint64_t n_subscriber_records) {
  for (const std::string &sql : tatp_create_sql("INTEGER", "INTEGER", "INTEGER",
                                                "INTEGER", "TEXT", true)) {
    conn.execute(sql).expect(SQLITE_OK);
  }

  sqlite::Statement subscriber;
  conn.prepare(subscriber, "INSERT INTO subscriber VALUES ("
                           "?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,"
                           "?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)")
      .expect(SQLITE_OK);

  sqlite::Statement access_info;
  conn.prepare(access_info, "INSERT INTO access_info VALUES (?,?,?,?,?,?)")
      .expect(SQLITE_OK);

  sqlite::Statement special_facility;
  conn.prepare(special_facility,
               "INSERT INTO special_facility VALUES (?,?,?,?,?,?)")
      .expect(SQLITE_OK);

  sqlite::Statement call_forwarding;
  conn.prepare(call_forwarding,
               "INSERT INTO call_forwarding VALUES (?,?,?,?,?)")
      .expect(SQLITE_OK);

  conn.begin();

  dbbench::tatp::RecordGenerator record_generator(n_subscriber_records);
  while (auto record = record_generator.next()) {
    std::visit(
        overloaded{
            [&](const dbbench::tatp::SubscriberRecord &r) {
              subscriber.bind_int64(1, (sqlite3_int64)r.s_id).expect(SQLITE_OK);
              subscriber.bind_text(2, r.sub_nbr).expect(SQLITE_OK);
              for (int i = 0; i < 10; ++i) {
                subscriber.bind_int(i + 3, r.bit[i]).expect(SQLITE_OK);
              }
              for (int i = 0; i < 10; ++i) {
                subscriber.bind_int(i + 13, r.hex[i]).expect(SQLITE_OK);
              }
              for (int i = 0; i < 10; ++i) {
                subscriber.bind_int(i + 23, r.byte2[i]).expect(SQLITE_OK);
              }
              subscriber.bind_int64(33, (sqlite3_int64)r.msc_location)
                  .expect(SQLITE_OK);
              subscriber.bind_int64(34, (sqlite3_int64)r.vlr_location)
                  .expect(SQLITE_OK);
              subscriber.execute().expect(SQLITE_OK);
            },

            [&](const dbbench::tatp::AccessInfoRecord &r) {
              access_info
                  .bind_all((sqlite3_int64)r.s_id, (int)r.ai_type, (int)r.data1,
                            (int)r.data2, r.data3.c_str(), r.data4.c_str())
                  .expect(SQLITE_OK);
              access_info.execute().expect(SQLITE_OK);
            },

            [&](const dbbench::tatp::SpecialFacilityRecord &r) {
              special_facility
                  .bind_all((sqlite3_int64)r.s_id, (int)r.sf_type,
                            (int)r.is_active, (int)r.error_cntrl, (int)r.data_a,
                            r.data_b.c_str())
                  .expect(SQLITE_OK);
              special_facility.execute().expect(SQLITE_OK);
            },

            [&](const dbbench::tatp::CallForwardingRecord &r) {
              call_forwarding
                  .bind_all((sqlite3_int64)r.s_id, (int)r.sf_type,
                            (int)r.start_time, (int)r.end_time,
                            r.numberx.c_str())
                  .expect(SQLITE_OK);
              call_forwarding.execute().expect(SQLITE_OK);
            },
        },
        *record);
  }

  conn.commit();
}

class Worker {
public:
  Worker(sqlite::Connection conn, uint64_t n_subscriber_records)
      : conn_(std::move(conn)), procedure_generator_(n_subscriber_records) {
    std::array<std::string, 10> sql = tatp_statement_sql();
    for (int i = 0; i < 10; ++i) {
      conn_.prepare(stmts_[i], sql[i]).expect(SQLITE_OK);
    }
  }

  bool operator()() {
    return std::visit(
        overloaded{
            [&](const dbbench::tatp::GetSubscriberData &p) {
              stmts_[0].bind_all((sqlite3_int64)p.s_id).expect(SQLITE_OK);
              stmts_[0].execute().expect(SQLITE_OK);
              return true;
            },

            [&](const dbbench::tatp::GetNewDestination &p) {
              stmts_[1]
                  .bind_all((sqlite3_int64)p.s_id, (int)p.sf_type,
                            (int)p.start_time, (int)p.end_time)
                  .expect(SQLITE_OK);
              size_t count;
              stmts_[1].execute(count).expect(SQLITE_OK);
              return count > 0;
            },

            [&](const dbbench::tatp::GetAccessData &p) {
              stmts_[2]
                  .bind_all((sqlite3_int64)p.s_id, (int)p.ai_type)
                  .expect(SQLITE_OK);
              size_t count = 0;
              stmts_[2].execute(count).expect(SQLITE_OK);
              return count > 0;
            },

            [&](const dbbench::tatp::UpdateSubscriberData &p) {
              conn_.begin().expect(SQLITE_OK);

              stmts_[3]
                  .bind_all((int)p.bit_1, (sqlite3_int64)p.s_id)
                  .expect(SQLITE_OK);
              stmts_[3].execute().expect(SQLITE_OK);

              stmts_[4]
                  .bind_all((int)p.data_a, (sqlite3_int64)p.s_id,
                            (int)p.sf_type)
                  .expect(SQLITE_OK);
              stmts_[4].execute().expect(SQLITE_OK);

              conn_.commit().expect(SQLITE_OK);

              return conn_.changes() > 0;
            },

            [&](const dbbench::tatp::UpdateLocation &p) {
              stmts_[5]
                  .bind_all((sqlite3_int64)p.vlr_location, p.sub_nbr.c_str())
                  .expect(SQLITE_OK);
              stmts_[5].execute().expect(SQLITE_OK);
              return true;
            },

            [&](const dbbench::tatp::InsertCallForwarding &p) {
              conn_.begin().expect(SQLITE_OK);

              stmts_[6].bind_all(p.sub_nbr.c_str()).expect(SQLITE_OK);
              stmts_[6].step().expect(SQLITE_ROW);
              uint64_t s_id = stmts_[6].column_int64(0);
              stmts_[6].reset().expect(SQLITE_OK);

              stmts_[7].bind(1, (sqlite3_int64)s_id).expect(SQLITE_OK);
              stmts_[7].execute().expect(SQLITE_OK);

              stmts_[8]
                  .bind_all((sqlite3_int64)s_id, (int)p.sf_type,
                            (int)p.start_time, (int)p.end_time,
                            p.numberx.c_str())
                  .expect(SQLITE_OK);
              sqlite::Result rc = stmts_[8].execute();
              bool success = true;
              if (rc != SQLITE_OK) {
                rc.expect(SQLITE_CONSTRAINT);
                success = false;
              }

              conn_.commit().expect(SQLITE_OK);

              return success;
            },

            [&](const dbbench::tatp::DeleteCallForwarding &p) {
              conn_.begin().expect(SQLITE_OK);

              stmts_[6].bind_all(p.sub_nbr.c_str()).expect(SQLITE_OK);
              stmts_[6].step().expect(SQLITE_ROW);
              uint64_t s_id = stmts_[6].column_int64(0);
              stmts_[6].reset().expect(SQLITE_OK);

              stmts_[9]
                  .bind_all((sqlite3_int64)s_id, (int)p.sf_type,
                            (int)p.start_time)
                  .expect(SQLITE_OK);
              stmts_[9].execute().expect(SQLITE_OK);

              conn_.commit().expect(SQLITE_OK);

              return conn_.changes() > 0;
            },
        },
        procedure_generator_.next());
  }

private:
  sqlite::Connection conn_;
  std::array<sqlite::Statement, 10> stmts_;
  dbbench::tatp::ProcedureGenerator procedure_generator_;
};

int main(int argc, char **argv) {
  cxxopts::Options options = tatp_options("tatp_sqlite3", "TATP on SQLite3");

  cxxopts::OptionAdder adder = options.add_options("SQLite3");
  adder("journal_mode", "Journal mode",
        cxxopts::value<std::string>()->default_value("DELETE"));
  adder("cache_size", "Cache size",
        cxxopts::value<std::string>()->default_value("1048576"));
  adder("wal_size", "WAL size (in pages)",
        cxxopts::value<std::string>()->default_value("1024"));
  adder("extension", "SQLite extension to be loaded",
        cxxopts::value<std::string>()->default_value(""));
  adder("oid", "Aurora OID",
      cxxopts::value<std::string>()->default_value("0"));

  cxxopts::ParseResult result = options.parse(argc, argv);

  if (result.count("help")) {
    std::cout << options.help();
    return 0;
  }

  auto n_subscriber_records = result["records"].as<uint64_t>();
  auto cache_size = result["cache_size"].as<std::string>();
  auto wal_size = result["wal_size"].as<std::string>();
  auto extension = result["extension"].as<std::string>();
  auto oid = result["oid"].as<std::string>();

  char fnamebuf[URI_MAXLEN];
  if (!extension.empty()) {
    sqlite::Connection conn;
    sqlite::Database db(":memory:");

    db.connect(conn).expect(SQLITE_OK);
    conn.enable_extensions();
    conn.load_extension(extension);

  size_t mapsize_bytes = std::stoi(cache_size) * 1024 * 1024;
  auto *name = "/sqlite.sas";
  auto status = slsfs_sas_create((char *)name, mapsize_bytes);
  if (status != 0) {
	printf("slsfs_sas_create failed (error %d)\n", status);
	exit(1);
  }

  auto fd = open(name, O_RDWR, 0666);
  if (fd < 0) {
  	perror("open");
  	exit(1);
  }

  void *addr;
  status = slsfs_sas_map(fd, (void **)&addr);
  if (status != 0) {
  	printf("slsfs_sas_map failed\n");
  	exit(1);
  }
  if (addr == NULL) {
	perror("mmap");
	exit(1);
  }

    /* Trigger the creation of the underlying object. */
    *(char *)addr = '1';

    snprintf(fnamebuf, URI_MAXLEN, "file:///tatp.sqlite3.db?ptr=%p&sz=%d&max=%ld&fd=%d",
		addr,
    		0,
    		mapsize_bytes,
    		fd);
  } else {
    snprintf(fnamebuf, URI_MAXLEN, "tatp.sqlite");
  }

  sqlite::Database db(fnamebuf);

  std::vector<Worker> workers;

  /* 
   * XXX Single client for now, as the AuroraVFS file type is not robust. 
   * The original benchmark runs with a single client as the default anyway.
   */
  sqlite::Connection conn;
  if (!extension.empty()) {
    db.connect(conn, extension).expect(SQLITE_OK);
    conn.execute("PRAGMA synchronous=NORMAL").expect(SQLITE_OK);
    conn.execute("PRAGMA journal_mode=OFF").expect(SQLITE_OK);
  } else {
    db.connect(conn).expect(SQLITE_OK);
    conn.execute("PRAGMA synchronous=NORMAL").expect(SQLITE_OK);
    conn.execute("PRAGMA journal_mode=WAL").expect(SQLITE_OK);
  }

  conn.execute("PRAGMA locking_mode=EXCLUSIVE").expect(SQLITE_OK);
  conn.execute("PRAGMA cache_size=" + cache_size).expect(SQLITE_OK);

  /* Load the schema and data from the same connection we use for benchmarking. */
  load(conn, n_subscriber_records);

  workers.emplace_back(std::move(conn), n_subscriber_records);

  double throughput = dbbench::run(workers, result["warmup"].as<size_t>(),
                                   result["measure"].as<size_t>());

  std::cout << throughput << std::endl;

  return 0;
}
