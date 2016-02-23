module Suica
  class Suica
    ACTION = {
      25 => "New card",
      22 => "Train",
      200 => "Vending Machine"
    }

    def read_date(data)
      x = data[4] << 8 | data[5]
      year = x >> 9
      month = (x >> 5) & 0x0f;
      day = x & 0x1f
      "#{2000 + year}-#{month}-#{day}"
    end

    def initialize(felica_device)
      @felica_device = felica_device
    end

    def read_transactions
      0.upto(32) do |x|
        block = @felica_device.read_block(0x090f, x).bytes
        break if block[0] == 0 # no more data

        action_name = ACTION[block[0]]
        date = read_date(block)
        balance = (block[11] << 8) + block[10]
        serial = (block[12] << 16) + (block[13] << 8) + block[14]

        puts [action_name, date, balance, serial.inspect].join(', ')
      end
    end
  end
end
