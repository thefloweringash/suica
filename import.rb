require 'felica'
require 'suica'
require 'sqlite3'

# Super naive transaction importer. Assumes serial is unique, and
# sufficient for transaction identity. The result is a sqlite database
# with each transaction recorded exactly once, but otherwise preserved
# as-is.

class DB
  def initialize(name)
    @db = SQLite3::Database.new(name)

    @db.execute <<-SQL
      create table if not exists history (
        serial INT PRIMARY KEY NOT NULL,
        data BLOB
      );
    SQL
  end

  def insert(tx)
    insert_statement = <<-SQL
      insert or replace into history
        (serial, data)
        VALUES(?, ?)
    SQL
    @db.execute(insert_statement, tx.serial, tx.raw)
  end
end

def import(db_name)
  db = DB.new(db_name)
  puts "Waiting for card..."
  suica = Suica::Suica.new(
    Felica::Felica.new.poll)

  puts "Importing transactions"
  transactions = suica.read_transactions()

  transactions.each do |tx|
    db.insert(tx)
  end

  puts "Imported #{transactions.count} transactions"
end

import "history.db"
