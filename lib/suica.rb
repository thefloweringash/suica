require 'date'

module Suica
  class Transaction
    ACTION = {
      25  => "New card",
      22  => "Train",
      200 => "Vending Machine"
    }

    attr_reader :raw, :serial, :date, :balance

    def read_date(data)
      x     = data[4] << 8 | data[5]
      year  = x >> 9
      month = (x >> 5) & 0x0f;
      day   = x & 0x1f
      Date.new(2000 + year, month, day)
    end

    def initialize(block)
      @raw  = block
      bytes = block.bytes

      @action  = bytes[0]
      @date    = read_date(bytes)
      @balance = (bytes[11] << 8) + bytes[10]
      @serial  = (bytes[12] << 16) + (bytes[13] << 8) + bytes[14]
    end
  end

  class Suica
    HISTORY_SERVICE = 0x090f

    def initialize(felica_device)
      @felica_device = felica_device
    end

    def read_transactions
      result = []
      tx = 0
      loop do
        block = @felica_device.read_block(HISTORY_SERVICE, tx)
        break if block.getbyte(0) == 0
        result << Transaction.new(block)
        tx = tx + 1
      end
      result
    end
  end
end
