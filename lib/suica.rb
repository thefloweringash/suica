require 'date'

##
# Suica is a transport card implemented with Felica.
#
# The Suica module contains logic specific to the transit system, such
# as reading transaction history.

module Suica
  ##
  # Transaction handler
  class Transaction
    ACTION = {
      25  => "New card",
      22  => "Train",
      200 => "Vending Machine"
    }

    # Raw bytes of the transaction
    attr_reader :raw

    # Serial number of the transaction
    attr_reader :serial

    # Date on which the transaction occured
    attr_reader :date

    # Absolute value of the balance after the transaction was completed.
    attr_reader :balance

    ##
    # Initialise from 16-bytes of raw transaction data.
    def initialize(block)
      @raw  = block
      bytes = block.bytes

      @action  = bytes[0]
      @date    = read_date(bytes)
      @balance = (bytes[11] << 8) + bytes[10]
      @serial  = (bytes[12] << 16) + (bytes[13] << 8) + bytes[14]
    end

    private
    def read_date(data)
      x     = data[4] << 8 | data[5]
      year  = x >> 9
      month = (x >> 5) & 0x0f;
      day   = x & 0x1f
      Date.new(2000 + year, month, day)
    end
  end

  class Suica
    # Felica service containing the transaction history
    HISTORY_SERVICE = 0x090f

    ##
    # Initialise Suica access with a Felica::Device.

    def initialize(felica_device)
      @felica_device = felica_device
    end

    ##
    # Read all available transactions. Returns an array of
    # Suica::Transaction.
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
