require 'date'

# Suica is a transport card implemented with Felica.
#
# The Suica module contains logic specific to the transit system, such
# as reading transaction history.

module Suica
  # Transaction data decoder
  class Transaction
    # Names of possible actions encoded in transactions
    ACTION_NAMES = {
      25  => 'New card',
      22  => 'Train',
      200 => 'Vending Machine'
    }.freeze

    # @return [String] raw bytes of the transaction
    attr_reader :raw

    # @return [Fixnum] serial number of the transaction
    attr_reader :serial

    # @return [Date] date on which the transaction occured
    attr_reader :date

    # @return [Fixum]
    attr_reader :action

    # @return [Fixnum]
    #   Absolute value of the balance after the transaction was completed.
    attr_reader :balance

    # Initialise from 16-bytes of raw transaction data.
    def initialize(block)
      @raw  = block
      bytes = block.bytes

      @action  = bytes[0]
      @date    = read_date(bytes)
      @balance = (bytes[11] << 8) + bytes[10]
      @serial  = (bytes[12] << 16) + (bytes[13] << 8) + bytes[14]
    end

    # @return [String, nil] name of action, if known
    def action_name
      ACTION_NAMES[action]
    end

    private

    def read_date(data)
      x     = data[4] << 8 | data[5]
      year  = x >> 9
      month = (x >> 5) & 0x0f
      day   = x & 0x1f
      Date.new(2000 + year, month, day)
    end
  end

  # Use a +Felica::Target+ as a Suica
  class Suica
    # Felica service containing the transaction history
    HISTORY_SERVICE = 0x090f

    # Initialise Suica access with a Felica::Target.
    def initialize(felica_target)
      @felica_target = felica_target
    end

    # @overload read_transactions(&block)
    #   Read all available transactions, yielding Suica::Transaction.
    #   @yieldparam transaction [Transaction]
    #   @return [void]
    #
    # @overload read_transactions()
    #   Read all available transactions
    #   @return [Enumerator<Transaction>] transactions

    def read_transactions
      return enum_for(:read_transactions) unless block_given?

      0.step do |tx|
        begin
          block = @felica_target.read_block(HISTORY_SERVICE, tx)
          break if block.getbyte(0).zero?
          yield Transaction.new(block)
        rescue Felica::FelicaStatusError => e
          break if e.s1 == 0x01 && e.s2 == 0xA8
          raise
        end
      end
    end
  end
end
