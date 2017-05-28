require 'forwardable'

# Felica device access.
#
# Wraps a minimal amount of libnfc and provides methods specific to
# felica targets.
#
# @example
#     require 'felica'
#     Felica.open_device do |nfc_device|
#       loop do
#         card = nfc_device.select_felica
#         ..
#       end
#     end

class Felica
  require 'felica/errors'
  require 'felica/felica'

  # A minimal binding to libnfc. Not sufficiently generic to handle tags
  # other than Felica.
  class NFC
    # Wraps a libnfc +nfc_context+.
    class Context
      # @overload open_device(&block)
      #
      #   Runs given block with an open +NFC::Device+, and closes on return.
      #
      #   @yieldparam nfc_device [NFC::Device]
      #
      # @overload open_device
      #
      #   Opens an +NFC::Device+ which should be manually closed when no longer
      #   in use, since it may prevent further access to the nfc hardware
      #
      #   @return [NFC::Device]
      def open_device
        device = open_device_raw.init!

        return device unless block_given?

        begin
          yield device
        ensure
          device.close
        end
      end
    end
  end

  class << self
    extend Forwardable

    # @api private
    # @return [NFC::Context] the global context
    attr_reader :nfc_context

    # @!method open_device
    #   @see NFC::Context#open_device
    delegate open_device: :nfc_context

    private

    def initialize_global_context!
      @nfc_context = NFC.make_context
    end
  end

  initialize_global_context!
end
